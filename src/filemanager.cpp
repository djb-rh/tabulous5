#include "filemanager.h"

#include <LittleFS.h>
#include <WebServer.h>
#include <dirent.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include "contentserver.h"
#include "sdcard.h"

namespace tabulous {
namespace filemanager {
namespace {

WebServer *g_server = nullptr;
uint32_t g_revision = 0;

// ---- volumes -------------------------------------------------------------

struct Volume {
  const char *id;
  const char *label;
};
constexpr Volume kVolumes[] = {{"sd", "SD card"}, {"fs", "Built-in"}};

fs::FS *volumeFs(const std::string &id) {
  if (id == "sd") return sdcard::mounted() ? &sdcard::fs() : nullptr;
  if (id == "fs") return (fs::FS *)&LittleFS;
  return nullptr;
}

uint64_t volumeFree(const std::string &id) {
  if (id == "fs") return LittleFS.totalBytes() - LittleFS.usedBytes();
  if (id == "sd" && sdcard::mounted()) {
    return sdcard::freeBytes();
  }
  return 0;
}

// ---- path safety -----------------------------------------------------------

// Absolute, no traversal, no control characters, no trailing slash except the
// root itself. Everything the browser sends is untrusted and this writes to
// the filesystems the games read from.
bool safePath(const std::string &p) {
  if (p.empty() || p[0] != '/') return false;
  if (p.find("..") != std::string::npos) return false;
  if (p.find("//") != std::string::npos) return false;
  for (char c : p) {
    if ((unsigned char)c < 0x20) return false;
  }
  if (p.size() > 1 && p.back() == '/') return false;
  return p.size() < 240;
}

// A single name: no slashes, no traversal, not a dotfile (the manager's own
// bookkeeping lives in dotfiles and they stay out of reach).
bool safeName(const std::string &n) {
  if (n.empty() || n.size() > 120) return false;
  if (n[0] == '.') return false;
  for (char c : n) {
    if (c == '/' || c == '\\' || (unsigned char)c < 0x20) return false;
  }
  return true;
}

std::string dirOf(const std::string &p) {
  const size_t slash = p.find_last_of('/');
  if (slash == 0) return "/";
  return p.substr(0, slash);
}

std::string baseOf(const std::string &p) {
  const size_t slash = p.find_last_of('/');
  return slash == std::string::npos ? p : p.substr(slash + 1);
}

std::string join(const std::string &dir, const std::string &name) {
  return dir == "/" ? "/" + name : dir + "/" + name;
}

std::string jsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) continue;
        out += c;
    }
  }
  return out;
}

void sendError(int code, const std::string &message) {
  g_server->send(code, "application/json",
                 ("{\"error\":\"" + jsonEscape(message) + "\"}").c_str());
}

void sendOk() { g_server->send(200, "application/json", "{\"ok\":true}"); }

// ---- .hidden ---------------------------------------------------------------

std::string hiddenPath(const std::string &dir) { return join(dir, ".hidden"); }

std::string readHidden(fs::FS &fs, const std::string &dir) {
  File f = fs.open(hiddenPath(dir).c_str(), "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return std::string(s.c_str());
}

bool isHidden(const std::string &hidden, const std::string &name) {
  size_t at = 0;
  while (at < hidden.size()) {
    size_t e = hidden.find('\n', at);
    if (e == std::string::npos) e = hidden.size();
    size_t n = e;
    while (n > at && (hidden[n - 1] == '\r' || hidden[n - 1] == ' ')) n--;
    if (n - at == name.size() && hidden.compare(at, n - at, name) == 0) return true;
    at = e + 1;
  }
  return false;
}

bool writeHidden(fs::FS &fs, const std::string &dir, const std::string &text) {
  const std::string path = hiddenPath(dir);
  if (text.empty()) {
    fs.remove(path.c_str());
    return true;
  }
  File f = fs.open(path.c_str(), "w");
  if (!f) return false;
  f.write((const uint8_t *)text.data(), text.size());
  f.close();
  return true;
}

// ---- handlers ----------------------------------------------------------------

// Resolves ?vol= and ?path= or answers with an error. Returns null on error.
fs::FS *volumeAndPath(std::string *vol, std::string *path, const char *path_arg = "path") {
  *vol = g_server->arg("vol").c_str();
  *path = g_server->arg(path_arg).c_str();
  fs::FS *fs = volumeFs(*vol);
  if (!fs) {
    sendError(400, *vol == "sd" ? "no card mounted" : "unknown volume");
    return nullptr;
  }
  if (!safePath(*path)) {
    sendError(400, "bad path");
    return nullptr;
  }
  return fs;
}

void handlePage();

void handleVolumes() {
  std::string out = "[";
  for (size_t i = 0; i < sizeof(kVolumes) / sizeof(kVolumes[0]); i++) {
    if (i) out += ",";
    const bool avail = volumeFs(kVolumes[i].id) != nullptr;
    out += "{\"id\":\"" + std::string(kVolumes[i].id) + "\",\"label\":\"" +
           kVolumes[i].label + "\",\"available\":" + (avail ? "true" : "false") + "}";
  }
  out += "]";
  g_server->send(200, "application/json", out.c_str());
}

// Entries come from readdir, not the Arduino File API: that API opens every
// entry it lists, and one open in a FAT directory of thousands of files is a
// linear search through the whole directory — the full ROM folder took
// minutes and blocked the console for all of it. readdir hands back names
// and types in a couple of seconds. Sizes cost a lookup each, so they are
// fetched only for folders small enough that it does not matter.
constexpr int kSizedFolderMax = 200;

const char *vfsRoot(const std::string &vol) {
  return vol == "sd" ? sdcard::mountPoint() : "/littlefs";
}

void handleList() {
  std::string vol, path;
  fs::FS *fs = volumeAndPath(&vol, &path);
  if (!fs) return;
  std::string vfs = std::string(vfsRoot(vol)) + (path == "/" ? "" : path);
  DIR *d = opendir(vfs.c_str());
  if (!d) {
    sendError(404, "no such folder");
    return;
  }
  const std::string hidden = readHidden(*fs, path);

  // Two passes: count first to decide about sizes, then emit.
  int count = 0;
  for (struct dirent *e = readdir(d); e; e = readdir(d)) {
    if (e->d_name[0] != '.') count++;
  }
  rewinddir(d);
  const bool sized = count <= kSizedFolderMax;

  // Streamed in chunks: a folder of thousands would not fit in one string.
  g_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_server->send(200, "application/json", "");
  std::string out = "{\"vol\":\"" + vol + "\",\"path\":\"" + jsonEscape(path) +
                    "\",\"free_bytes\":" + std::to_string((unsigned long long)volumeFree(vol)) +
                    ",\"count\":" + std::to_string(count) + ",\"entries\":[";
  bool first = true;
  for (struct dirent *e = readdir(d); e; e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    const bool dir = e->d_type == DT_DIR;
    std::string size = "null";
    if (sized && !dir) {
      struct stat st;
      const std::string full = vfs + "/" + e->d_name;
      if (stat(full.c_str(), &st) == 0) size = std::to_string((unsigned long long)st.st_size);
    }
    if (!first) out += ",";
    first = false;
    out += "{\"name\":\"" + jsonEscape(e->d_name) + "\",\"type\":\"" + (dir ? "dir" : "file") +
           "\",\"size\":" + size +
           ",\"hidden\":" + (isHidden(hidden, e->d_name) ? "true" : "false") + "}";
    if (out.size() > 4096) {
      g_server->sendContent(out.c_str());
      out.clear();
    }
  }
  closedir(d);
  out += "]}";
  g_server->sendContent(out.c_str());
  g_server->sendContent("");
}

void handleMkdir() {
  std::string vol, path;
  fs::FS *fs = volumeAndPath(&vol, &path);
  if (!fs) return;
  const std::string name = g_server->arg("name").c_str();
  if (!safeName(name)) {
    sendError(400, "bad name");
    return;
  }
  if (!fs->mkdir(join(path, name).c_str())) {
    sendError(500, "could not create folder");
    return;
  }
  g_revision++;
  sendOk();
}

void handleRename() {
  std::string vol, path;
  fs::FS *fs = volumeAndPath(&vol, &path);
  if (!fs) return;
  const std::string name = g_server->arg("name").c_str();
  if (path == "/" || !safeName(name)) {
    sendError(400, "bad name");
    return;
  }
  const std::string to = join(dirOf(path), name);
  if (fs->exists(to.c_str())) {
    sendError(409, "something by that name already exists");
    return;
  }
  if (!fs->rename(path.c_str(), to.c_str())) {
    sendError(500, "rename failed");
    return;
  }
  // A hidden file keeps being hidden under its new name.
  const std::string dir = dirOf(path);
  std::string hidden = readHidden(*fs, dir);
  if (isHidden(hidden, baseOf(path))) {
    std::string rebuilt;
    size_t at = 0;
    while (at < hidden.size()) {
      size_t e = hidden.find('\n', at);
      if (e == std::string::npos) e = hidden.size();
      std::string line = hidden.substr(at, e - at);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (line == baseOf(path)) line = name;
      if (!line.empty()) rebuilt += line + "\n";
      at = e + 1;
    }
    writeHidden(*fs, dir, rebuilt);
  }
  g_revision++;
  sendOk();
}

bool removeTree(fs::FS &fs, const std::string &path, int depth) {
  if (depth > 8) return false;
  File f = fs.open(path.c_str());
  if (!f) return false;
  const bool dir = f.isDirectory();
  if (dir) {
    // Collect first: deleting while iterating confuses the directory walk.
    std::vector<std::string> children;
    for (File c = f.openNextFile(); c; c = f.openNextFile()) {
      std::string name = c.name();
      const size_t slash = name.find_last_of('/');
      if (slash != std::string::npos) name = name.substr(slash + 1);
      children.push_back(name);
      c.close();
    }
    f.close();
    for (const std::string &name : children) {
      if (!removeTree(fs, join(path, name), depth + 1)) return false;
    }
    return fs.rmdir(path.c_str());
  }
  f.close();
  return fs.remove(path.c_str());
}

void handleDelete() {
  std::string vol, path;
  fs::FS *fs = volumeAndPath(&vol, &path);
  if (!fs) return;
  if (path == "/" || path == "/packs" || path == "/sfx" || path == "/roms") {
    sendError(400, "that folder stays");
    return;
  }
  if (!removeTree(*fs, path, 0)) {
    sendError(500, "delete failed");
    return;
  }
  g_revision++;
  sendOk();
}

void handleHide() {
  std::string vol, path;
  fs::FS *fs = volumeAndPath(&vol, &path);
  if (!fs) return;
  const bool hide = g_server->arg("hidden") == "1";
  const std::string dir = dirOf(path), name = baseOf(path);
  std::string hidden = readHidden(*fs, dir);
  std::string rebuilt;
  size_t at = 0;
  while (at < hidden.size()) {
    size_t e = hidden.find('\n', at);
    if (e == std::string::npos) e = hidden.size();
    std::string line = hidden.substr(at, e - at);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!line.empty() && line != name) rebuilt += line + "\n";
    at = e + 1;
  }
  if (hide) rebuilt += name + "\n";
  if (!writeHidden(*fs, dir, rebuilt)) {
    sendError(500, "could not write .hidden");
    return;
  }
  g_revision++;
  sendOk();
}

// ---- upload --------------------------------------------------------------------
//
// Streamed straight to a temporary file in the target folder as the chunks
// arrive, then renamed over any existing file of that name at the end, so a
// connection that drops halfway leaves no half-written ROM behind.

File g_up;
std::string g_up_tmp, g_up_final;
bool g_up_ok = false;
const char *g_up_error = "";

void handleUploadData() {
  HTTPUpload &up = g_server->upload();
  if (up.status == UPLOAD_FILE_START) {
    g_up_ok = false;
    g_up_error = "";
    std::string vol = g_server->arg("vol").c_str();
    std::string path = g_server->arg("path").c_str();
    fs::FS *fs = volumeFs(vol);
    std::string name = up.filename.c_str();
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (!fs || !safePath(path) || !safeName(name)) {
      g_up_error = "bad volume, folder or name";
      return;
    }
    g_up_tmp = join(path, ".upload.tmp");
    g_up_final = join(path, name);
    fs->remove(g_up_tmp.c_str());
    g_up = fs->open(g_up_tmp.c_str(), "w");
    if (!g_up) g_up_error = "could not create the file";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_up && up.currentSize) {
      if (g_up.write(up.buf, up.currentSize) != up.currentSize) {
        g_up.close();
        g_up_error = "write failed (out of space?)";
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (g_up) {
      g_up.close();
      fs::FS *fs = volumeFs(g_server->arg("vol").c_str());
      if (fs) {
        fs->remove(g_up_final.c_str());
        g_up_ok = fs->rename(g_up_tmp.c_str(), g_up_final.c_str());
        if (!g_up_ok) g_up_error = "could not move the upload into place";
      }
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (g_up) g_up.close();
    fs::FS *fs = volumeFs(g_server->arg("vol").c_str());
    if (fs && !g_up_tmp.empty()) fs->remove(g_up_tmp.c_str());
    g_up_error = "upload aborted";
  }
}

void handleUploadDone() {
  if (!g_up_ok) {
    sendError(500, g_up_error[0] ? g_up_error : "upload failed");
    return;
  }
  g_revision++;
  g_server->send(200, "application/json",
                 ("{\"ok\":true,\"path\":\"" + jsonEscape(g_up_final) + "\"}").c_str());
}

// ---- page ----------------------------------------------------------------------

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Tabulous5 files</title>
<style>
:root{color-scheme:dark}
body{margin:0;font:15px/1.5 system-ui,sans-serif;background:#0e1116;color:#f5f7fa}
header{padding:14px 18px;background:#1a1f27;display:flex;gap:12px;align-items:center;flex-wrap:wrap}
h1{font-size:16px;margin:0;font-weight:700;letter-spacing:.06em}
main{padding:14px 18px;max-width:980px}
select,button,input{font:inherit;background:#252c37;color:#f5f7fa;border:1px solid #39404d;border-radius:8px;padding:8px 12px}
button{cursor:pointer}
button:disabled{opacity:.5;cursor:default}
button.primary{background:#30a46c;border-color:#30a46c}
a{color:#7cc4ff;text-decoration:none}
#editor{background:#252c37;border:1px solid #39404d;border-radius:8px;padding:8px 12px;color:#f5f7fa}
.crumbs{color:#8892a0;margin:4px 0 12px;word-break:break-all}
.crumbs a{cursor:pointer}
.bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:12px}
.space{margin-left:auto;color:#8892a0;font-size:13px}
table{width:100%;border-collapse:collapse}
td{padding:8px 10px;border-top:1px solid #1a1f27;vertical-align:middle}
tr.hidden td.nm{color:#8892a0;text-decoration:line-through}
td.nm{word-break:break-all}
td.nm a{cursor:pointer;color:#f5f7fa}
td.sz{color:#8892a0;font-size:13px;white-space:nowrap;text-align:right}
td.acts{white-space:nowrap;text-align:right}
td.acts button{padding:5px 9px;font-size:13px;margin-left:4px}
td.acts button.del{border-color:#e5484d;color:#ff8f92}
.empty{padding:40px 0;text-align:center;color:#8892a0}
#msg{padding:10px 12px;border-radius:8px;margin-bottom:12px;display:none}
#msg.ok{display:block;background:#173a2a;color:#8fe3b4}
#msg.err{display:block;background:#4a1d1f;color:#ffb3b5}
.prog{height:6px;background:#252c37;border-radius:3px;overflow:hidden;margin-bottom:12px;display:none}
.prog i{display:block;height:100%;background:#30a46c;width:0}
.drop{border:2px dashed #39404d;border-radius:10px;padding:14px;text-align:center;color:#8892a0;margin-bottom:12px}
.drop.over{border-color:#30a46c;color:#f5f7fa}
</style>
<header>
  <h1>TABULOUS5 FILES</h1>
  <select id=vol></select>
  <button id=up class=primary>Upload files</button>
  <button id=mkdir>New folder</button>
  <a id=editor href="/">Word packs</a>
</header>
<main>
<div class=crumbs id=crumbs></div>
<div id=msg></div>
<div class=prog id=prog><i id=progbar></i></div>
<div class=drop id=drop>Drop files here to upload them to this folder</div>
<input type=file id=picker multiple style="display:none">
<div class=bar><span class=space id=space></span></div>
<div id=list></div>
</main>
<script>
var vol = 'sd', cwd = '/';
function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML;}
function fmtSize(n){if(n==null)return'';var u=['B','KB','MB','GB'],i=0;while(n>=1024&&i<u.length-1){n/=1024;i++;}return(i?n.toFixed(1):n)+' '+u[i];}
function note(kind,text){var m=document.getElementById('msg');m.className=kind;m.textContent=text;if(kind==='ok')setTimeout(function(){m.className='';},4000);}
function q(o){return Object.keys(o).map(function(k){return k+'='+encodeURIComponent(o[k]);}).join('&');}
function post(url,args){return fetch(url+'?'+q(args),{method:'POST'}).then(function(r){return r.json().then(function(d){return{ok:r.ok,d:d};});});}

function crumbs(path){
  var out='<a onclick="go(\'/\')">'+esc(document.getElementById('vol').selectedOptions[0].textContent)+'</a>';
  var acc='';
  path.split('/').filter(Boolean).forEach(function(part){acc+='/'+part;out+=' / <a onclick="go(\''+acc.replace(/'/g,"\\'")+'\')">'+esc(part)+'</a>';});
  return out;
}
function go(path){
  cwd=path||'/';
  fetch('/files/list?'+q({vol:vol,path:cwd}))
    .then(function(r){return r.json().then(function(d){return{ok:r.ok,d:d};});})
    .then(function(res){if(!res.ok){note('err',res.d.error||'Could not open folder');return;}render(res.d);})
    .catch(function(){note('err','Could not reach the console');});
}
function render(d){
  cwd=d.path;
  document.getElementById('crumbs').innerHTML=crumbs(cwd);
  document.getElementById('space').textContent=(d.count!=null?d.count+' items  \u00b7  ':'')+(d.free_bytes!=null?fmtSize(d.free_bytes)+' free':'');
  if(!d.entries.length){document.getElementById('list').innerHTML='<div class=empty>This folder is empty.</div>';return;}
  d.entries.sort(function(a,b){if(a.type!==b.type)return a.type==='dir'?-1:1;return a.name.localeCompare(b.name,undefined,{sensitivity:'base'});});
  var h='<table>';
  d.entries.forEach(function(e){
    var p=(cwd==='/'?'':cwd)+'/'+e.name, ps=p.replace(/'/g,"\\'"), ns=e.name.replace(/'/g,"\\'");
    h+='<tr'+(e.hidden?' class=hidden':'')+'><td class=nm>'+(e.type==='dir'?'&#128193; <a onclick="go(\''+ps+'\')">'+esc(e.name)+'</a>':esc(e.name))+'</td>';
    h+='<td class=sz>'+(e.type==='dir'?'':fmtSize(e.size))+'</td><td class=acts>';
    if(e.type!=='dir')h+='<button onclick="hide(\''+ps+'\','+(e.hidden?0:1)+')">'+(e.hidden?'Show':'Hide')+'</button>';
    h+='<button onclick="rn(\''+ps+'\',\''+ns+'\')">Rename</button><button class=del onclick="rm(\''+ps+'\',\''+(e.type==='dir'?'folder':'file')+'\')">Delete</button></td></tr>';
  });
  document.getElementById('list').innerHTML=h+'</table>';
}
function hide(path,on){post('/files/hide',{vol:vol,path:path,hidden:on}).then(function(res){if(!res.ok){note('err',res.d.error||'Failed');return;}go(cwd);});}
function rn(path,current){var name=prompt('New name:',current);if(!name||name===current)return;post('/files/rename',{vol:vol,path:path,name:name}).then(function(res){if(!res.ok){note('err',res.d.error||'Rename failed');return;}note('ok','Renamed.');go(cwd);});}
function rm(path,kind){if(!confirm('Delete this '+kind+'?'+(kind==='folder'?'\n\nEverything inside it will be deleted too.':'')))return;post('/files/delete',{vol:vol,path:path}).then(function(res){if(!res.ok){note('err',res.d.error||'Delete failed');return;}note('ok','Deleted.');go(cwd);});}
document.getElementById('mkdir').onclick=function(){var name=prompt('Folder name:');if(!name)return;post('/files/mkdir',{vol:vol,path:cwd,name:name}).then(function(res){if(!res.ok){note('err',res.d.error||'Could not create folder');return;}go(cwd);});};
document.getElementById('up').onclick=function(){document.getElementById('picker').click();};
document.getElementById('picker').onchange=function(){upload(Array.prototype.slice.call(this.files));this.value='';};
var drop=document.getElementById('drop');
drop.ondragover=function(e){e.preventDefault();drop.classList.add('over');};
drop.ondragleave=function(){drop.classList.remove('over');};
drop.ondrop=function(e){e.preventDefault();drop.classList.remove('over');upload(Array.prototype.slice.call(e.dataTransfer.files));};

// One request per file, and each file re-read into a Blob first: Safari
// sends an empty body for a File taken straight from an input.
function readFile(f){return new Promise(function(res,rej){var fr=new FileReader();fr.onload=function(){res(fr.result);};fr.onerror=function(){rej(new Error('unreadable'));};fr.readAsArrayBuffer(f);});}
function sendOne(f,buf,onProgress){return new Promise(function(resolve){var fd=new FormData();fd.append('file',new Blob([buf],{type:'application/octet-stream'}),f.name);var xhr=new XMLHttpRequest();xhr.open('POST','/files/upload?'+q({vol:vol,path:cwd}));xhr.upload.onprogress=function(e){if(e.lengthComputable)onProgress(e.loaded/e.total);};xhr.onload=function(){var d={};try{d=JSON.parse(xhr.responseText);}catch(_){}resolve({ok:xhr.status===200,data:d,status:xhr.status});};xhr.onerror=function(){resolve({ok:false,data:{error:'connection lost'},status:0});};xhr.send(fd);});}
function upload(files){
  if(!files.length)return;
  var prog=document.getElementById('prog'),bar=document.getElementById('progbar'),btn=document.getElementById('up');
  prog.style.display='block';bar.style.width='0';btn.disabled=true;
  var saved=0,failed=[];
  (async function(){
    for(var i=0;i<files.length;i++){
      var f=files[i],base=i/files.length;
      try{var buf=await readFile(f);var res=await sendOne(f,buf,function(frac){bar.style.width=((base+frac/files.length)*100)+'%';});
        if(res.ok)saved++;else failed.push(f.name+' ('+(res.data.error||res.status)+')');}
      catch(e){failed.push(f.name+' (unreadable)');}
      bar.style.width=(((i+1)/files.length)*100)+'%';
    }
    prog.style.display='none';btn.disabled=false;
    var parts=[];if(saved)parts.push(saved+' file'+(saved===1?'':'s')+' uploaded.');if(failed.length)parts.push('Failed: '+failed.join(', ')+'.');
    note(failed.length?'err':'ok',parts.join(' ')||'Nothing to upload.');go(cwd);
  })();
}
fetch('/files/volumes').then(function(r){return r.json();}).then(function(vs){
  var sel=document.getElementById('vol');
  vs.forEach(function(v){var o=document.createElement('option');o.value=v.id;o.textContent=v.label+(v.available?'':' (none)');o.disabled=!v.available;sel.appendChild(o);});
  var first=vs.filter(function(v){return v.available;})[0];vol=first?first.id:'fs';sel.value=vol;
  sel.onchange=function(){vol=sel.value;go('/');};
  go('/');
});
</script>
)HTML";

void handlePage() { g_server->send_P(200, "text/html", kPage); }

void routes(WebServer &server) {
  g_server = &server;
  server.on("/files", HTTP_GET, handlePage);
  server.on("/files/volumes", HTTP_GET, handleVolumes);
  server.on("/files/list", HTTP_GET, handleList);
  server.on("/files/mkdir", HTTP_POST, handleMkdir);
  server.on("/files/rename", HTTP_POST, handleRename);
  server.on("/files/delete", HTTP_POST, handleDelete);
  server.on("/files/hide", HTTP_POST, handleHide);
  server.on("/files/upload", HTTP_POST, handleUploadDone, handleUploadData);
}

}  // namespace

void begin() { contentserver::addRouteHook(routes); }
uint32_t revision() { return g_revision; }

}  // namespace filemanager
}  // namespace tabulous
