#include "filemanager.h"

#include <LittleFS.h>
#include <WebServer.h>
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <string>
#include <vector>

#include "contentserver.h"
#include "nes_ui.h"
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

const char *vfsRoot(const std::string &vol) {
  return vol == "sd" ? sdcard::mountPoint() : "/littlefs";
}

uint64_t volumeFree(const std::string &id) {
  if (id == "fs") return LittleFS.totalBytes() - LittleFS.usedBytes();
  if (id == "sd" && sdcard::mounted()) return sdcard::freeBytes();
  return 0;
}

// ---- path safety ---------------------------------------------------------

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

// A single name: no slashes, no traversal, not a dotfile — the manager's own
// bookkeeping lives in dotfiles and they stay out of reach.
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

// ---- name lists ----------------------------------------------------------
//
// Both the per-folder `.hidden` file and the console's favourites file are
// plain lists of one file name per line, so they can be read and edited by
// hand. The request body that names what to act on has the same shape.

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at < text.size()) {
    size_t e = text.find('\n', at);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(at, e - at);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!line.empty()) out.push_back(line);
    at = e + 1;
  }
  return out;
}

std::string joinLines(const std::vector<std::string> &lines) {
  std::string out;
  for (const std::string &l : lines) out += l + "\n";
  return out;
}

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

std::string readList(fs::FS &fs, const std::string &path) {
  File f = fs.open(path.c_str(), "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return std::string(s.c_str());
}

bool writeList(fs::FS &fs, const std::string &path, const std::vector<std::string> &lines) {
  if (lines.empty()) {
    fs.remove(path.c_str());
    return true;
  }
  const std::string text = joinLines(lines);
  File f = fs.open(path.c_str(), "w");
  if (!f) return false;
  const bool ok = f.write((const uint8_t *)text.data(), text.size()) == text.size();
  f.close();
  return ok;
}

std::string hiddenPath(const std::string &dir) { return join(dir, ".hidden"); }

// Folders a game or the console itself expects to find. Deleting one from a
// browser would be a long walk back.
bool isProtected(const std::string &p) {
  static const char *const kKeep[] = {"/", "/packs", "/sfx", "/nes", "/gb"};
  for (const char *k : kKeep) {
    if (p == k) return true;
  }
  return false;
}

// Adds or removes `names` from a list file, and says whether anything changed.
bool editList(fs::FS &fs, const std::string &path, const std::vector<std::string> &names,
              bool add, int *changed) {
  std::vector<std::string> lines = splitLines(readList(fs, path));
  *changed = 0;
  for (const std::string &n : names) {
    const bool have = contains(lines, n);
    if (add && !have) {
      lines.push_back(n);
      (*changed)++;
    } else if (!add && have) {
      lines.erase(std::remove(lines.begin(), lines.end(), n), lines.end());
      (*changed)++;
    }
  }
  if (*changed == 0) return true;
  return writeList(fs, path, lines);
}

// ---- request helpers -----------------------------------------------------

// The names to act on arrive as the request body, one per line, rather than in
// the query string: a selection here can be thousands of files long.
std::vector<std::string> bodyNames() {
  std::vector<std::string> names;
  if (!g_server->hasArg("plain")) return names;
  for (const std::string &n : splitLines(g_server->arg("plain").c_str())) {
    if (safeName(n)) names.push_back(n);
  }
  return names;
}

// Resolves ?vol= and ?<arg>=, answering with an error if either is bad.
fs::FS *volumeAndPath(std::string *vol, std::string *path, const char *arg = "path") {
  *vol = g_server->arg("vol").c_str();
  *path = g_server->arg(arg).c_str();
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

void sendCount(const char *key, int n) {
  g_server->send(200, "application/json",
                 ("{\"ok\":true,\"" + std::string(key) + "\":" + std::to_string(n) + "}").c_str());
}

// ---- handlers ------------------------------------------------------------

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
// linear search through the whole directory — the full ROM folder took minutes
// and blocked the console for all of it. readdir hands back names and types in
// a couple of seconds. Sizes cost a lookup each, so they are fetched only for
// folders small enough that it does not matter.
constexpr int kSizedFolderMax = 200;

void handleList() {
  std::string vol, path;
  fs::FS *fs = volumeAndPath(&vol, &path);
  if (!fs) return;
  const std::string vfs = std::string(vfsRoot(vol)) + (path == "/" ? "" : path);
  DIR *d = opendir(vfs.c_str());
  if (!d) {
    sendError(404, "no such folder");
    return;
  }
  const std::vector<std::string> hidden = splitLines(readList(*fs, hiddenPath(path)));
  const std::vector<std::string> favs =
      splitLines(readList(LittleFS, nes_ui::kFavouritesFile));

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
           ",\"hidden\":" + (contains(hidden, e->d_name) ? "true" : "false") +
           ",\"fav\":" + (contains(favs, e->d_name) ? "true" : "false") + "}";
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
  sendCount("made", 1);
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
  const std::string dir = dirOf(path), was = baseOf(path);
  const std::string to = join(dir, name);
  if (fs->exists(to.c_str())) {
    sendError(409, "something by that name already exists");
    return;
  }
  if (!fs->rename(path.c_str(), to.c_str())) {
    sendError(500, "rename failed");
    return;
  }
  // Hidden stays hidden and a favourite stays a favourite under the new name.
  int changed = 0;
  const std::vector<std::string> one_was{was}, one_now{name};
  if (contains(splitLines(readList(*fs, hiddenPath(dir))), was)) {
    editList(*fs, hiddenPath(dir), one_was, false, &changed);
    editList(*fs, hiddenPath(dir), one_now, true, &changed);
  }
  if (contains(splitLines(readList(LittleFS, nes_ui::kFavouritesFile)), was)) {
    editList(LittleFS, nes_ui::kFavouritesFile, one_was, false, &changed);
    editList(LittleFS, nes_ui::kFavouritesFile, one_now, true, &changed);
  }
  g_revision++;
  sendCount("renamed", 1);
}

void handleHide() {
  std::string vol, dir;
  fs::FS *fs = volumeAndPath(&vol, &dir, "dir");
  if (!fs) return;
  const std::vector<std::string> names = bodyNames();
  if (names.empty()) {
    sendError(400, "nothing named");
    return;
  }
  int changed = 0;
  if (!editList(*fs, hiddenPath(dir), names, g_server->arg("hidden") == "1", &changed)) {
    sendError(500, "could not write .hidden");
    return;
  }
  if (changed) g_revision++;
  sendCount("changed", changed);
}

void handleFavourite() {
  const std::vector<std::string> names = bodyNames();
  if (names.empty()) {
    sendError(400, "nothing named");
    return;
  }
  int changed = 0;
  if (!editList(LittleFS, nes_ui::kFavouritesFile, names, g_server->arg("fav") == "1",
                &changed)) {
    sendError(500, "could not write the favourites file");
    return;
  }
  if (changed) g_revision++;
  sendCount("changed", changed);
}

bool removeTree(fs::FS &fs, const std::string &path, int depth) {
  if (depth > 8) return false;
  File f = fs.open(path.c_str());
  if (!f) return false;
  if (!f.isDirectory()) {
    f.close();
    return fs.remove(path.c_str());
  }
  // Collect first: deleting while iterating confuses the directory walk.
  std::vector<std::string> children;
  for (File c = f.openNextFile(); c; c = f.openNextFile()) {
    children.push_back(baseOf(c.name()));
    c.close();
  }
  f.close();
  for (const std::string &name : children) {
    if (!removeTree(fs, join(path, name), depth + 1)) return false;
  }
  return fs.rmdir(path.c_str());
}

void handleDelete() {
  std::string vol, dir;
  fs::FS *fs = volumeAndPath(&vol, &dir, "dir");
  if (!fs) return;
  const std::vector<std::string> names = bodyNames();
  if (names.empty()) {
    sendError(400, "nothing named");
    return;
  }
  int gone = 0;
  std::string failed;
  for (size_t i = 0; i < names.size(); i++) {
    const std::string path = join(dir, names[i]);
    if (isProtected(path)) continue;
    if (removeTree(*fs, path, 0)) {
      gone++;
    } else if (failed.size() < 120) {
      failed += (failed.empty() ? "" : ", ") + names[i];
    }
    // A selection here can be thousands of files, and each removal is a FAT
    // write. Yield regularly so the idle task runs and its watchdog stays fed.
    if ((i & 0x0F) == 0) delay(1);
  }
  if (gone) g_revision++;
  if (!failed.empty()) {
    sendError(500, "deleted " + std::to_string(gone) + ", failed: " + failed);
    return;
  }
  sendCount("deleted", gone);
}

// ---- upload --------------------------------------------------------------
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
    const std::string vol = g_server->arg("vol").c_str();
    const std::string path = g_server->arg("path").c_str();
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

// ---- page ----------------------------------------------------------------

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Tabulous5 files</title>
<style>
:root{color-scheme:dark}
body{margin:0;font:15px/1.5 system-ui,sans-serif;background:#0e1116;color:#f5f7fa}
header{padding:14px 18px;background:#1a1f27;display:flex;gap:12px;align-items:center;flex-wrap:wrap}
h1{font-size:16px;margin:0;font-weight:700;letter-spacing:.06em}
main{padding:14px 18px;max-width:1000px}
select,button,input{font:inherit;background:#252c37;color:#f5f7fa;border:1px solid #39404d;border-radius:8px;padding:8px 12px}
button{cursor:pointer}
button:disabled{opacity:.45;cursor:default}
button.primary{background:#30a46c;border-color:#30a46c}
button.on{background:#3b4453;border-color:#5b6577}
button.del:not(:disabled){border-color:#e5484d;color:#ff8f92}
a{color:#7cc4ff;text-decoration:none}
#editor{background:#252c37;border:1px solid #39404d;border-radius:8px;padding:8px 12px;color:#f5f7fa}
.crumbs{color:#8892a0;margin:4px 0 12px;word-break:break-all}
.crumbs a{cursor:pointer}
.bar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:10px}
.bar label{display:flex;gap:7px;align-items:center;cursor:pointer;white-space:nowrap}
.dim{color:#8892a0;font-size:13px}
.space{margin-left:auto;color:#8892a0;font-size:13px}
input[type=checkbox]{width:17px;height:17px;padding:0;accent-color:#30a46c}
table{width:100%;border-collapse:collapse}
td{padding:7px 10px;border-top:1px solid #1a1f27;vertical-align:middle}
tr.hid td.nm{color:#8892a0}
td.ck{width:24px;padding-right:0}
td.nm{word-break:break-all}
td.nm a{cursor:pointer;color:#f5f7fa}
td.tag{white-space:nowrap;color:#8892a0;font-size:12px;text-align:right}
td.tag b{color:#ffc53d;font-weight:400}
td.sz{color:#8892a0;font-size:13px;white-space:nowrap;text-align:right}
td.acts{white-space:nowrap;text-align:right;width:90px}
td.acts button{padding:4px 9px;font-size:13px}
.empty{padding:40px 0;text-align:center;color:#8892a0}
#msg{padding:10px 12px;border-radius:8px;margin-bottom:12px;display:none}
#msg.ok{display:block;background:#173a2a;color:#8fe3b4}
#msg.err{display:block;background:#4a1d1f;color:#ffb3b5}
.prog{height:6px;background:#252c37;border-radius:3px;overflow:hidden;margin-bottom:12px;display:none}
.prog i{display:block;height:100%;background:#30a46c;width:0}
.drop{border:2px dashed #39404d;border-radius:10px;padding:12px;text-align:center;color:#8892a0;margin-bottom:12px;cursor:pointer}
.drop:hover,.drop.over{border-color:#30a46c;color:#f5f7fa}
.drop u{text-decoration:none;color:#7cc4ff}
</style>
<header>
  <h1>TABULOUS5 FILES</h1>
  <select id=vol></select>
  <button id=up class=primary>Upload files</button>
  <button id=mkdir>New folder</button>
  <a id=editor href="/packs">Word packs</a>
</header>
<main>
<div class=crumbs id=crumbs></div>
<div id=msg></div>
<div class=prog id=prog><i id=progbar></i></div>
<div class=drop id=drop>Drop files here to upload them to this folder, or <u>click to choose them</u></div>
<input type=file id=picker multiple style="display:none">
<div class=bar>
  <label><input type=checkbox id=all> Select all</label>
  <span class=dim id=selcount>none selected</span>
  <button class=act id=b-hide>Hide</button>
  <button class=act id=b-unhide>Unhide</button>
  <button class=act id=b-fav>Favourite</button>
  <button class=act id=b-unfav>Unfavourite</button>
  <button class="act del" id=b-del>Delete</button>
</div>
<div class=bar>
  <span class=dim>Show</span>
  <button class=view data-v=all>All</button>
  <button class=view data-v=hidden>Hidden</button>
  <button class=view data-v=fav>Favourites</button>
  <span class=space id=space></span>
</div>
<div id=list></div>
</main>
<script>
var vol='sd', cwd='/', view='all', ENTRIES=[], sel={}, nsel=0;
function $(id){return document.getElementById(id);}
function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML;}
function fmtSize(n){if(n==null)return'';var u=['B','KB','MB','GB'],i=0;while(n>=1024&&i<u.length-1){n/=1024;i++;}return(i?n.toFixed(1):n)+' '+u[i];}
function note(kind,text){var m=$('msg');m.className=kind;m.textContent=text;if(kind==='ok')setTimeout(function(){m.className='';},5000);}
function q(o){return Object.keys(o).map(function(k){return k+'='+encodeURIComponent(o[k]);}).join('&');}

// Names go in the body, one per line: a selection here can be thousands long
// and would not survive a query string.
function act(url,args,names){
  return fetch(url+'?'+q(args),{method:'POST',headers:{'Content-Type':'text/plain'},body:names.join('\n')})
    .then(function(r){return r.json().then(function(d){return{ok:r.ok,d:d};});});
}
function post(url,args){
  return fetch(url+'?'+q(args),{method:'POST'})
    .then(function(r){return r.json().then(function(d){return{ok:r.ok,d:d};});});
}

function crumbs(){
  var out='<a onclick="go(\'/\')">'+esc($('vol').selectedOptions[0].textContent)+'</a>',acc='';
  cwd.split('/').filter(Boolean).forEach(function(part){
    acc+='/'+part;
    out+=' / <a onclick="go(\''+acc.replace(/'/g,"\\'")+'\')">'+esc(part)+'</a>';
  });
  return out;
}

function go(path){
  cwd=path||'/'; sel={}; nsel=0;
  $('list').innerHTML='<div class=empty>Reading&hellip;</div>';
  fetch('/files/list?'+q({vol:vol,path:cwd}))
    .then(function(r){return r.json().then(function(d){return{ok:r.ok,d:d};});})
    .then(function(res){
      if(!res.ok){note('err',res.d.error||'Could not open folder');return;}
      cwd=res.d.path;
      ENTRIES=res.d.entries;
      ENTRIES.sort(function(a,b){
        if(a.type!==b.type)return a.type==='dir'?-1:1;
        return a.name.localeCompare(b.name,undefined,{sensitivity:'base'});
      });
      $('space').textContent=res.d.count+' item'+(res.d.count===1?'':'s')+
        (res.d.free_bytes!=null?'  \u00b7  '+fmtSize(res.d.free_bytes)+' free':'');
      render();
    })
    .catch(function(){note('err','Could not reach the console');});
}

function visible(){
  return ENTRIES.filter(function(e){
    if(view==='hidden')return e.hidden;
    if(view==='fav')return e.fav;
    return true;
  });
}

function render(){
  $('crumbs').innerHTML=crumbs();
  Array.prototype.forEach.call(document.querySelectorAll('.view'),function(b){
    b.classList.toggle('on',b.dataset.v===view);
  });
  var rows=visible();
  if(!rows.length){
    $('list').innerHTML='<div class=empty>'+
      (view==='hidden'?'Nothing hidden in this folder.':
       view==='fav'?'No favourites in this folder.':'This folder is empty.')+'</div>';
    counts();return;
  }
  var h='<table>';
  rows.forEach(function(e){
    var ns=e.name.replace(/'/g,"\\'");
    h+='<tr'+(e.hidden?' class=hid':'')+'>';
    h+='<td class=ck>'+(e.type==='dir'?'':'<input type=checkbox data-n="'+esc(e.name)+'"'+(sel[e.name]?' checked':'')+'>')+'</td>';
    h+='<td class=nm>'+(e.type==='dir'
      ?'&#128193; <a onclick="go(\''+((cwd==='/'?'':cwd)+'/'+e.name).replace(/'/g,"\\'")+'\')">'+esc(e.name)+'</a>'
      :esc(e.name))+'</td>';
    h+='<td class=tag>'+(e.fav?'<b>&#9733;</b> ':'')+(e.hidden?'hidden':'')+'</td>';
    h+='<td class=sz>'+(e.type==='dir'?'':fmtSize(e.size))+'</td>';
    h+='<td class=acts><button onclick="rn(\''+ns+'\')">Rename</button></td></tr>';
  });
  $('list').innerHTML=h+'</table>';
  counts();
}

function counts(){
  nsel=Object.keys(sel).length;
  $('selcount').textContent=nsel?nsel+' selected':'none selected';
  ['b-hide','b-unhide','b-fav','b-unfav','b-del'].forEach(function(id){$(id).disabled=!nsel;});
  var vis=visible().filter(function(e){return e.type!=='dir';});
  $('all').checked=vis.length>0&&vis.every(function(e){return sel[e.name];});
}

$('list').addEventListener('change',function(ev){
  var cb=ev.target;
  if(cb.type!=='checkbox')return;
  if(cb.checked)sel[cb.dataset.n]=1;else delete sel[cb.dataset.n];
  counts();
});

$('all').onclick=function(){
  var on=$('all').checked;
  visible().forEach(function(e){
    if(e.type==='dir')return;
    if(on)sel[e.name]=1;else delete sel[e.name];
  });
  render();
};

Array.prototype.forEach.call(document.querySelectorAll('.view'),function(b){
  b.onclick=function(){view=b.dataset.v;render();};
});

function names(){return Object.keys(sel);}

function bulk(url,args,verb){
  var n=names();
  act(url,args,n).then(function(res){
    if(!res.ok){note('err',res.d.error||'Failed');return;}
    note('ok',(res.d.changed!=null?res.d.changed:n.length)+' '+verb+'.');
    go(cwd);
  });
}
$('b-hide').onclick=function(){bulk('/files/hide',{vol:vol,dir:cwd,hidden:1},'hidden');};
$('b-unhide').onclick=function(){bulk('/files/hide',{vol:vol,dir:cwd,hidden:0},'shown again');};
$('b-fav').onclick=function(){bulk('/files/favourite',{fav:1},'added to favourites');};
$('b-unfav').onclick=function(){bulk('/files/favourite',{fav:0},'removed from favourites');};
$('b-del').onclick=function(){
  var n=names();
  if(!confirm('Delete '+n.length+' item'+(n.length===1?'':'s')+'?\n\nThis cannot be undone.'))return;
  bulk('/files/delete',{vol:vol,dir:cwd},'deleted');
};

function rn(current){
  var name=prompt('New name:',current);
  if(!name||name===current)return;
  post('/files/rename',{vol:vol,path:(cwd==='/'?'':cwd)+'/'+current,name:name}).then(function(res){
    if(!res.ok){note('err',res.d.error||'Rename failed');return;}
    note('ok','Renamed.');go(cwd);
  });
}

$('mkdir').onclick=function(){
  var name=prompt('Folder name:');
  if(!name)return;
  post('/files/mkdir',{vol:vol,path:cwd,name:name}).then(function(res){
    if(!res.ok){note('err',res.d.error||'Could not create folder');return;}
    go(cwd);
  });
};

$('up').onclick=function(){$('picker').click();};
$('picker').onchange=function(){upload(Array.prototype.slice.call(this.files));this.value='';};
var drop=$('drop');
drop.onclick=function(){$('picker').click();};
drop.ondragover=function(e){e.preventDefault();drop.classList.add('over');};
drop.ondragleave=function(){drop.classList.remove('over');};
drop.ondrop=function(e){e.preventDefault();drop.classList.remove('over');upload(Array.prototype.slice.call(e.dataTransfer.files));};

// One request per file, and each file re-read into a Blob first: Safari sends
// an empty body for a File taken straight from an input.
function readFile(f){return new Promise(function(res,rej){var fr=new FileReader();fr.onload=function(){res(fr.result);};fr.onerror=function(){rej(new Error('unreadable'));};fr.readAsArrayBuffer(f);});}
function sendOne(f,buf,onProgress){
  return new Promise(function(resolve){
    var fd=new FormData();
    fd.append('file',new Blob([buf],{type:'application/octet-stream'}),f.name);
    var xhr=new XMLHttpRequest();
    xhr.open('POST','/files/upload?'+q({vol:vol,path:cwd}));
    xhr.upload.onprogress=function(e){if(e.lengthComputable)onProgress(e.loaded/e.total);};
    xhr.onload=function(){var d={};try{d=JSON.parse(xhr.responseText);}catch(_){}resolve({ok:xhr.status===200,data:d,status:xhr.status});};
    xhr.onerror=function(){resolve({ok:false,data:{error:'connection lost'},status:0});};
    xhr.send(fd);
  });
}
function upload(files){
  if(!files.length)return;
  var prog=$('prog'),bar=$('progbar'),btn=$('up');
  prog.style.display='block';bar.style.width='0';btn.disabled=true;
  var saved=0,failed=[];
  (async function(){
    for(var i=0;i<files.length;i++){
      var f=files[i],base=i/files.length;
      try{
        var buf=await readFile(f);
        var res=await sendOne(f,buf,function(frac){bar.style.width=((base+frac/files.length)*100)+'%';});
        if(res.ok)saved++;else failed.push(f.name+' ('+(res.data.error||res.status)+')');
      }catch(e){failed.push(f.name+' (unreadable)');}
      bar.style.width=(((i+1)/files.length)*100)+'%';
    }
    prog.style.display='none';btn.disabled=false;
    var parts=[];
    if(saved)parts.push(saved+' file'+(saved===1?'':'s')+' uploaded.');
    if(failed.length)parts.push('Failed: '+failed.join(', ')+'.');
    note(failed.length?'err':'ok',parts.join(' ')||'Nothing to upload.');
    go(cwd);
  })();
}

fetch('/files/volumes').then(function(r){return r.json();}).then(function(vs){
  var s=$('vol');
  vs.forEach(function(v){
    var o=document.createElement('option');
    o.value=v.id;o.textContent=v.label+(v.available?'':' (none)');o.disabled=!v.available;
    s.appendChild(o);
  });
  var first=vs.filter(function(v){return v.available;})[0];
  vol=first?first.id:'fs';s.value=vol;
  s.onchange=function(){vol=s.value;view='all';go('/');};
  go('/');
});
</script>
)HTML";

void handlePage() { g_server->send_P(200, "text/html", kPage); }

void routes(WebServer &server) {
  g_server = &server;
  server.on("/", HTTP_GET, handlePage);
  server.on("/files", HTTP_GET, handlePage);
  server.on("/files/volumes", HTTP_GET, handleVolumes);
  server.on("/files/list", HTTP_GET, handleList);
  server.on("/files/mkdir", HTTP_POST, handleMkdir);
  server.on("/files/rename", HTTP_POST, handleRename);
  server.on("/files/delete", HTTP_POST, handleDelete);
  server.on("/files/hide", HTTP_POST, handleHide);
  server.on("/files/favourite", HTTP_POST, handleFavourite);
  server.on("/files/upload", HTTP_POST, handleUploadDone, handleUploadData);
}

}  // namespace

void begin() { contentserver::addRouteHook(routes); }
uint32_t revision() { return g_revision; }

}  // namespace filemanager
}  // namespace tabulous
