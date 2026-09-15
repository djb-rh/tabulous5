#include "contentserver.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

namespace tabulous {
namespace contentserver {
namespace {

constexpr uint32_t kConnectTimeoutMs = 25000;
constexpr const char *kHostname = "tabulous5";
// Open deliberately: a captive portal behind a password is a lot of
// friction at a party, and the hotspot only exists while the editor
// screen is open. It serves word lists, not secrets.
constexpr const char *kApSsid = "Tabulous5";
constexpr uint8_t kDnsPort = 53;

std::vector<Root> g_roots;
WebServer *g_server = nullptr;
std::vector<RouteHook> g_hooks;
State g_state = State::Off;
uint32_t g_started_ms = 0;
uint32_t g_revision = 0;
std::string g_error;
bool g_mdns = false;
DNSServer *g_dns = nullptr;
Mode g_mode = Mode::Auto;
bool g_ap = false;
std::string g_ssid, g_password;

// ------------------------------------------------------------- path safety

// A path is only acceptable if it sits directly inside a registered root and
// contains no traversal. Everything the browser sends is untrusted, and this
// server can write to the filesystem the games read from.
bool safePath(const std::string &path, std::string *why) {
  if (path.find("..") != std::string::npos) {
    if (why) *why = "path traversal";
    return false;
  }
  if (path.empty() || path[0] != '/') {
    if (why) *why = "path must be absolute";
    return false;
  }
  for (const Root &r : g_roots) {
    if (path.size() <= r.path.size() + 1) continue;
    if (path.compare(0, r.path.size(), r.path) != 0) continue;
    if (path[r.path.size()] != '/') continue;
    // Directly inside the root, not in a subdirectory of it.
    if (path.find('/', r.path.size() + 1) != std::string::npos) {
      if (why) *why = "subdirectories are not editable";
      return false;
    }
    return true;
  }
  if (why) *why = "path is not inside an editable directory";
  return false;
}

bool knownRoot(const std::string &dir) {
  for (const Root &r : g_roots) {
    if (r.path == dir) return true;
  }
  return false;
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
        if ((unsigned char)c < 0x20) continue;  // drop other control chars
        out += c;
    }
  }
  return out;
}

// ------------------------------------------------------------------- page

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Tabulous5 content</title>
<style>
:root{color-scheme:dark}
body{margin:0;font:15px/1.5 system-ui,sans-serif;background:#0e1116;color:#f5f7fa}
header{padding:14px 18px;background:#1a1f27;display:flex;gap:12px;align-items:center;flex-wrap:wrap}
h1{font-size:16px;margin:0;font-weight:700;letter-spacing:.06em}
main{display:flex;gap:14px;padding:14px;flex-wrap:wrap}
#side{flex:1 1 240px;min-width:220px}
#edit{flex:3 1 460px;min-width:300px}
select,button,input{font:inherit;background:#252c37;color:#f5f7fa;border:1px solid #39404d;border-radius:8px;padding:8px 12px}
button{cursor:pointer}
button.primary{background:#30a46c;border-color:#30a46c}
button.danger{background:#e5484d;border-color:#e5484d}
#files{background:#252c37;border:1px solid #39404d;border-radius:8px;padding:8px 12px;color:#f5f7fa;text-decoration:none}
ul{list-style:none;margin:8px 0 0;padding:0;max-height:60vh;overflow:auto}
li{padding:9px 11px;border-radius:8px;cursor:pointer;display:flex;justify-content:space-between;gap:8px}
li:hover{background:#1a1f27}
li.on{background:#252c37;font-weight:600}
li span{color:#8892a0;font-weight:400;font-size:13px}
textarea{width:100%;height:58vh;box-sizing:border-box;background:#12161d;color:#f5f7fa;border:1px solid #39404d;border-radius:10px;padding:12px;font:14px/1.55 ui-monospace,Menlo,monospace;resize:vertical}
.row{display:flex;gap:8px;align-items:center;margin-top:10px;flex-wrap:wrap}
#msg{margin-left:auto;color:#8892a0}
#back{position:fixed;inset:0;background:rgba(0,0,0,.62);display:none;
  align-items:center;justify-content:center;padding:18px;z-index:9}
#back.on{display:flex}
#help-box{background:#1a1f27;border:1px solid #39404d;border-radius:12px;
  max-width:640px;width:100%;max-height:84vh;overflow:auto;padding:20px 22px}
#help-box h2{margin:0 0 4px;font-size:17px}
#help-box h3{margin:18px 0 6px;font-size:14px;color:#ffc53d;
  text-transform:uppercase;letter-spacing:.08em}
#help-box pre{background:#12161d;border:1px solid #39404d;border-radius:8px;
  padding:11px 13px;overflow:auto;font:13px/1.5 ui-monospace,Menlo,monospace;
  margin:0}
#help-box ul.h{list-style:disc;margin:0;padding-left:20px;max-height:none}
#help-box ul.h li{display:list-item;padding:3px 0;cursor:auto;border-radius:0}
#help-box ul.h li:hover{background:none}
#help-box code{background:#12161d;border-radius:4px;padding:1px 5px;
  font:13px ui-monospace,Menlo,monospace}
.hrow{display:flex;justify-content:space-between;align-items:center;gap:12px}
</style>
<header>
  <h1>TABULOUS5</h1>
  <select id=root></select>
  <button id=new>New file</button>
  <button id=help>Help</button>
  <a id=files href="/">Files</a>
  <span id=msg></span>
</header>

<div id=back>
 <div id=help-box>
  <div class=hrow>
   <h2>Word pack format</h2>
   <button id=help-close>Close</button>
  </div>
  <p>One phrase per line. Everything else is optional.</p>
  <pre># name: Movies &amp; TV
# color: #E4572E
# icon: film

Jurassic Park
The Great British Bake Off | easy
Eternal Sunshine of the Spotless Mind | hard</pre>

  <h3>Lines</h3>
  <ul class=h>
   <li><code># key: value</code> sets the pack's <code>name</code>,
       <code>color</code> (hex) or <code>icon</code>. Unknown keys are ignored.</li>
   <li>A <code>#</code> line with no colon is just a comment.</li>
   <li><code>| easy</code>, <code>| medium</code> or <code>| hard</code> after a
       phrase sets its difficulty. Without one it is medium.</li>
   <li>Blank lines are skipped, surrounding spaces are trimmed, and Windows
       line endings are fine.</li>
  </ul>

  <h3>Caveats</h3>
  <ul class=h>
   <li>A difficulty it does not recognise becomes <b>medium</b>, silently — so a
       typo costs you the tag rather than raising an error.</li>
   <li><code>|</code> is the difficulty separator, so a phrase containing one
       gets cut at it. Avoid it in phrase text.</li>
   <li>With no <code># name:</code>, the filename is used as the pack name.</li>
   <li><b>Delete is immediate and cannot be undone.</b></li>
   <li>Text only. The game's sound effects are not editable here.</li>
   <li>Saving writes a temporary file and renames it, so a crash or a dropped
       Wi-Fi connection mid-save cannot leave a half-written pack.</li>
   <li>The device reloads packs when you tap <b>FINISH</b> on it, not on every
       save — so finish up here first.</li>
  </ul>
 </div>
</div>
<main>
  <div id=side><ul id=files></ul></div>
  <div id=edit>
    <textarea id=text spellcheck=false placeholder="Select a file"></textarea>
    <div class=row>
      <button class=primary id=save>Save</button>
      <button class=danger id=del>Delete</button>
      <span id=info></span>
    </div>
  </div>
</main>
<script>
let cur=null, roots=[], dirty=false;
const $=id=>document.getElementById(id);
const msg=(t,ms=2500)=>{$('msg').textContent=t;if(ms)setTimeout(()=>{if($('msg').textContent===t)$('msg').textContent=''},ms)};
async function j(u,o){const r=await fetch(u,o);if(!r.ok)throw new Error(await r.text());return r};
const showHelp=on=>$('back').classList.toggle('on',on);
$('help').onclick=()=>showHelp(true);
$('help-close').onclick=()=>showHelp(false);
// Click the backdrop, not the panel, to dismiss.
$('back').onclick=e=>{if(e.target===$('back'))showHelp(false)};
addEventListener('keydown',e=>{if(e.key==='Escape')showHelp(false)});

async function boot(){
  roots=await(await j('/api/roots')).json();
  $('root').innerHTML=roots.map(r=>`<option value="${r.path}">${r.label}</option>`).join('');
  await list();
}
async function list(){
  const dir=$('root').value;
  const files=await(await j('/api/list?dir='+encodeURIComponent(dir))).json();
  $('files').innerHTML=files.map(f=>`<li data-p="${f.path}" class="${f.path===cur?'on':''}">${f.name}<span>${f.size}B</span></li>`).join('')
    ||'<li><span>no files yet</span></li>';
  document.querySelectorAll('#files li[data-p]').forEach(li=>li.onclick=()=>open(li.dataset.p));
}
async function open(p){
  if(dirty&&!confirm('Discard unsaved changes?'))return;
  cur=p;dirty=false;
  $('text').value=await(await j('/api/file?path='+encodeURIComponent(p))).text();
  $('info').textContent=p;
  await list();
}
$('text').oninput=()=>{dirty=true};
$('root').onchange=()=>{cur=null;$('text').value='';$('info').textContent='';list()};
$('save').onclick=async()=>{
  if(!cur)return msg('No file selected');
  await j('/api/file?path='+encodeURIComponent(cur),{method:'POST',body:$('text').value});
  dirty=false;msg('Saved');await list();
};
$('del').onclick=async()=>{
  if(!cur)return;
  if(!confirm('Delete '+cur+'?'))return;
  await j('/api/file?path='+encodeURIComponent(cur),{method:'DELETE'});
  cur=null;$('text').value='';$('info').textContent='';msg('Deleted');await list();
};
$('new').onclick=async()=>{
  const r=roots.find(x=>x.path===$('root').value);
  let n=prompt('New file name');if(!n)return;
  if(r.extension&&!n.endsWith(r.extension))n+=r.extension;
  const p=r.path+'/'+n;
  await j('/api/file?path='+encodeURIComponent(p),{method:'POST',body:'# name: '+n.replace(r.extension,'')+'\n# color: #4C9F70\n# icon: house\n'});
  await list();await open(p);
};
addEventListener('beforeunload',e=>{if(dirty)e.preventDefault()});
boot().catch(e=>msg('Error: '+e.message,0));
</script>
)HTML";

// ---------------------------------------------------------------- handlers

void sendJsonError(int code, const std::string &message) {
  g_server->send(code, "application/json",
                 ("{\"error\":\"" + jsonEscape(message) + "\"}").c_str());
}

void handleRoots() {
  std::string out = "[";
  for (size_t i = 0; i < g_roots.size(); i++) {
    if (i) out += ",";
    out += "{\"path\":\"" + jsonEscape(g_roots[i].path) + "\",\"label\":\"" +
           jsonEscape(g_roots[i].label) + "\",\"extension\":\"" +
           jsonEscape(g_roots[i].extension) + "\"}";
  }
  out += "]";
  g_server->send(200, "application/json", out.c_str());
}

void handleList() {
  const std::string dir = g_server->arg("dir").c_str();
  if (!knownRoot(dir)) {
    sendJsonError(400, "unknown directory");
    return;
  }
  File d = LittleFS.open(dir.c_str());
  std::string out = "[";
  bool first = true;
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      if (f.isDirectory()) {
        f.close();
        continue;
      }
      std::string name = f.name();
      const size_t slash = name.find_last_of('/');
      if (slash != std::string::npos) name = name.substr(slash + 1);
      if (!first) out += ",";
      first = false;
      out += "{\"name\":\"" + jsonEscape(name) + "\",\"path\":\"" +
             jsonEscape(dir + "/" + name) +
             "\",\"size\":" + std::to_string((unsigned)f.size()) + "}";
      f.close();
    }
    d.close();
  }
  out += "]";
  g_server->send(200, "application/json", out.c_str());
}

void handleGetFile() {
  const std::string path = g_server->arg("path").c_str();
  std::string why;
  if (!safePath(path, &why)) {
    sendJsonError(400, why);
    return;
  }
  File f = LittleFS.open(path.c_str(), "r");
  if (!f) {
    sendJsonError(404, "not found");
    return;
  }
  g_server->streamFile(f, "text/plain");
  f.close();
}

void handlePostFile() {
  const std::string path = g_server->arg("path").c_str();
  std::string why;
  if (!safePath(path, &why)) {
    sendJsonError(400, why);
    return;
  }
  // WebServer only exposes a raw body as arg("plain") when it did NOT parse
  // the request as a form. A form-encoded POST therefore arrives with no body
  // visible here — and writing that as-is truncated the file to zero while
  // still reporting success, silently emptying a pack. Refuse instead.
  if (!g_server->hasArg("plain")) {
    sendJsonError(415,
                  "send the file body with Content-Type: text/plain "
                  "(a form-encoded body would silently write an empty file)");
    return;
  }
  const String body = g_server->arg("plain");

  // Write to a temporary and rename, so a crash or a dropped connection
  // mid-save cannot leave a half-written pack that fails to parse.
  const std::string tmp = path + ".tmp";
  File f = LittleFS.open(tmp.c_str(), "w");
  if (!f) {
    sendJsonError(500, "cannot open for writing");
    return;
  }
  const size_t written = f.write((const uint8_t *)body.c_str(), body.length());
  f.close();
  if (written != body.length()) {
    LittleFS.remove(tmp.c_str());
    sendJsonError(507, "write failed — filesystem full?");
    return;
  }
  LittleFS.remove(path.c_str());
  if (!LittleFS.rename(tmp.c_str(), path.c_str())) {
    LittleFS.remove(tmp.c_str());
    sendJsonError(500, "rename failed");
    return;
  }

  g_revision++;
  g_server->send(200, "application/json", "{\"ok\":true}");
}

void handleDeleteFile() {
  const std::string path = g_server->arg("path").c_str();
  std::string why;
  if (!safePath(path, &why)) {
    sendJsonError(400, why);
    return;
  }
  if (!LittleFS.remove(path.c_str())) {
    sendJsonError(404, "not found");
    return;
  }
  g_revision++;
  g_server->send(200, "application/json", "{\"ok\":true}");
}

void routeFile() {
  switch (g_server->method()) {
    case HTTP_GET: handleGetFile(); break;
    case HTTP_POST: handlePostFile(); break;
    case HTTP_DELETE: handleDeleteFile(); break;
    default: sendJsonError(405, "method not allowed");
  }
}

void beginServer() {
  g_server = new WebServer(80);
  // Hooks first: WebServer gives the URL to whichever handler was registered
  // first, so the file manager claims "/" as the front door. The editor's own
  // "/" below is the fallback for a build with no file manager in it, which
  // also keeps the captive-portal redirect (which points at "/") from
  // bouncing off a 404 forever.
  for (RouteHook h : g_hooks) h(*g_server);
  g_server->on("/packs", HTTP_GET, []() {
    g_server->send_P(200, "text/html", kPage);
  });
  g_server->on("/", HTTP_GET, []() {
    g_server->send_P(200, "text/html", kPage);
  });
  g_server->on("/api/roots", HTTP_GET, handleRoots);
  g_server->on("/api/list", HTTP_GET, handleList);
  g_server->on("/api/file", routeFile);
  g_server->onNotFound([]() {
    // Phones probe a known URL to decide whether a network is "captive".
    // Redirecting anything we don't serve is what makes the sign-in sheet
    // pop up automatically instead of the user having to type an IP.
    if (g_ap) {
      const String where = "http://" + WiFi.softAPIP().toString() + "/";
      g_server->sendHeader("Location", where);
      g_server->send(302, "text/plain", "");
      return;
    }
    sendJsonError(404, "no such endpoint");
  });
  g_server->begin();

  g_mdns = MDNS.begin(kHostname);
  if (g_mdns) MDNS.addService("http", "tcp", 80);
}

void startHotspot() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(kApSsid);
  g_ap = true;

  g_dns = new DNSServer();
  // Wildcard: every lookup resolves to us, which is the other half of what
  // makes the captive-portal prompt appear.
  g_dns->setErrorReplyCode(DNSReplyCode::NoError);
  g_dns->start(kDnsPort, "*", WiFi.softAPIP());

  beginServer();
  g_state = State::Running;
  Serial.printf("[net] hotspot up ssid=%s ip=%s\n", kApSsid,
                WiFi.softAPIP().toString().c_str());
}

}  // namespace

void addRouteHook(RouteHook hook) {
  if (hook) g_hooks.push_back(hook);
}

void addRoot(const char *path, const char *label, const char *extension) {
  for (Root &r : g_roots) {
    if (r.path == path) {
      r.label = label;
      r.extension = extension;
      return;
    }
  }
  g_roots.push_back({path, label, extension ? extension : ""});
}

const std::vector<Root> &roots() { return g_roots; }

void start(Mode mode, const char *ssid, const char *password) {
  if (g_state == State::Running || g_state == State::Connecting) return;
  g_error.clear();
  g_mode = mode;
  g_ssid = ssid ? ssid : "";
  g_password = password ? password : "";
  g_started_ms = millis();

  if (mode == Mode::Hotspot) {
    startHotspot();
    return;
  }

  g_state = State::Connecting;
  Serial.printf("[net] joining \"%s\" (mode=%d)\n", g_ssid.c_str(),
                (int)mode);
  // Pins come from the board variant's BOARD_SDIO_ESP_HOSTED_* defines, so
  // WiFi over the C6 co-processor needs no explicit setPins() here.
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_ssid.c_str(), g_password.c_str());
}

void switchMode(Mode mode) {
  const std::string ssid = g_ssid, password = g_password;
  stop();
  start(mode, ssid.c_str(), password.c_str());
}

Mode mode() { return g_mode; }
bool usingHotspot() { return g_ap; }
const char *hotspotSsid() { return kApSsid; }

void stop() {
  if (g_state != State::Off) Serial.println("[net] stopping");
  if (g_dns) {
    g_dns->stop();
    delete g_dns;
    g_dns = nullptr;
  }
  g_ap = false;
  if (g_server) {
    g_server->stop();
    delete g_server;
    g_server = nullptr;
  }
  if (g_mdns) {
    MDNS.end();
    g_mdns = false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  g_state = State::Off;
}

void loop() {
  switch (g_state) {
    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        beginServer();
        g_state = State::Running;
        Serial.printf("[net] joined, serving on http://%s/  mdns=%d\n",
                      WiFi.localIP().toString().c_str(), (int)g_mdns);
      } else if (millis() - g_started_ms > kConnectTimeoutMs) {
        // disconnect(false), not (true): true also powers the radio off, and
        // the co-processor's transport cannot be brought up a second time in
        // one boot - its buffer pool fails to allocate and the console
        // asserts and reboots. The hotspot is started with the radio still up.
        WiFi.disconnect(false);
        Serial.printf("[net] join timed out after %ums (wifi status=%d)\n",
                      (unsigned)kConnectTimeoutMs, (int)WiFi.status());
        if (g_mode == Mode::Auto) {
          // The whole point of Auto: no usable network is not a failure, it
          // just means the device hosts its own.
          startHotspot();
        } else {
          g_error = "could not join the network";
          g_state = State::Failed;
        }
      }
      break;
    case State::Running:
      if (g_ap) {
        if (g_dns) g_dns->processNextRequest();
        if (g_server) g_server->handleClient();
        break;
      }
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[net] connection dropped");
        g_error = "lost the network";
        stop();
        g_state = State::Failed;
        break;
      }
      if (g_server) g_server->handleClient();
      break;
    default:
      break;
  }
}

State state() { return g_state; }
uint32_t revision() { return g_revision; }
const char *lastError() { return g_error.c_str(); }

std::string ip() {
  if (g_ap) return std::string(WiFi.softAPIP().toString().c_str());
  return WiFi.status() == WL_CONNECTED
             ? std::string(WiFi.localIP().toString().c_str())
             : std::string();
}

std::string url() {
  const std::string addr = ip();
  if (addr.empty()) return {};
  return "http://" + addr + "/";
}

}  // namespace contentserver
}  // namespace tabulous
