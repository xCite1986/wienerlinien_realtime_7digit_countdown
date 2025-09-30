/*
  Wiener Linien Countdown + NeoPixel 7-Segment mit REST-API + OSM-Karte
  - Setup/Status-Webseite
  - Live-Countdown (MM:SS) farbig wie LEDs
  - LED-Steuerung (Power, Helligkeit, Farben, Schwellwerte), Standby
  - LittleFS formatieren (Button)
  - Countdown nur aus timeReal vs serverTime (kein API-countdown)
  - Robuste String-Suche (timeReal, serverTime, coordinates)
  - HTTPS via BearSSL (optional Fingerprint, sonst insecure)
  - OTA + mDNS, LittleFS
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>

// ---------- Hardware ----------
#define LED_PIN         D4
#define NUM_DIGITS      4
#define NUM_SEGMENTS    7
#define LEDS_PER_SEG    2
#define NUM_LEDS        (NUM_DIGITS*NUM_SEGMENTS*LEDS_PER_SEG)

// ---------- LED Strip ----------
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------- Web ----------
ESP8266WebServer server(80);

// ---------- Konfiguration/Status ----------
struct AppConfig {
  String ssid = "";
  String password = "";
  String apiKey = "";
  String rbl = "";
  // LED Basis
  uint8_t brightness = 40;
  bool ledPower = true;
  // Farbe & Schwellen (Sekunden)
  uint16_t tLow = 30;
  uint16_t tMid = 90;
  uint8_t  colLow[3]  = {255, 0,   0};
  uint8_t  colMid[3]  = {255, 180, 0};
  uint8_t  colHigh[3] = {0,   255, 0};
  // HTTPS
  bool httpsInsecure = false;
  String httpsFingerprint = "";
  // nur für NTP-Stempel (ohne TZ-Config, WL serverTime maßgeblich)
  String tz = "CET-1CEST,M3.5.0,M10.5.0/3";
} cfg;

String lastDisplayValue = "----";
String ringLog;
String lastPayload;
unsigned long lastUpdateMs = 0;
int secondsToBus = 0;
bool blinkState = false;

unsigned long nextPollAt = 0;
unsigned long nextTickAt = 0;
unsigned long backoffMs = 20000;

bool timeSynced = false;
bool standby = false;

bool fsMounted = false;

// Halte die zuletzt gefundenen Stop-Koordinaten (GeoJSON: lon,lat)
bool haveCoords = false;
double stopLon = 0.0, stopLat = 0.0;

const time_t TIME_VALID_EPOCH = 1700000000; // ~2023-11-14

// ---------- 7-Segment Mapping ----------
const uint8_t digitSegmentMap[4][14] PROGMEM = {
  { 0,1,  2,3,  4,5,  6,7,  8,9, 10,11, 12,13 },
  {14,15, 16,17, 18,19, 20,21, 22,23, 24,25, 26,27 },
  {28,29, 30,31, 32,33, 34,35, 36,37, 38,39, 40,41 },
  {42,43, 44,45, 46,47, 48,49, 50,51, 52,53, 54,55 }
};
const uint8_t digitPatterns[10][7] PROGMEM = {
  {1,1,1,1,1,1,0},{0,1,1,0,0,0,0},{1,1,0,1,1,0,1},{1,1,1,1,0,0,1},{0,1,1,0,0,1,1},
  {1,0,1,1,0,1,1},{1,0,1,1,1,1,1},{1,1,1,0,0,0,0},{1,1,1,1,1,1,1},{1,1,1,1,0,1,1}
};

// ---------- HTML ----------
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>WL Display</title>
<link rel="preconnect" href="https://unpkg.com" />
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>
:root{--bg:#0b0f14;--card:#121821;--ink:#eaf2ff;--dim:#a9b7cf;--accent:#3aa6ff;--ok:#7ad96b;--warn:#f5c06a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font-family:system-ui,Segoe UI,Roboto,Helvetica,Arial,sans-serif}
.wrap{max-width:1100px;margin:0 auto;padding:20px}
.grid{display:grid;gap:16px}
@media(min-width:900px){.grid-2{grid-template-columns:1fr 1fr}}
.card{background:linear-gradient(180deg,#131a24,#0f1520);border:1px solid #1f2a38;border-radius:16px;padding:clamp(14px,2.5vw,22px);box-shadow:0 10px 30px rgba(0,0,0,.35)}
h1{margin:0 0 6px;font-size:clamp(18px,4.5vw,24px)} p.lead{margin:0 0 14px;color:var(--dim)}
label{font-size:13px;color:var(--dim);display:block;margin-bottom:6px}
input,textarea,select{width:100%;padding:12px;border-radius:12px;border:1px solid #253245;background:#0e1420;color:#eaf2ff;font-size:16px}
input[type=range]{padding:0;height:36px}
textarea{min-height:160px;resize:vertical;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
input:focus,textarea:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 4px rgba(58,166,255,.15)}
.row{display:grid;gap:10px}
.row-2{display:grid;gap:10px;grid-template-columns:1fr 1fr}
.btn{display:inline-flex;align-items:center;justify-content:center;background:linear-gradient(180deg,#2a67ff,#154eff);color:#fff;border:none;
     padding:12px 16px;border-radius:12px;font-weight:600;cursor:pointer}
.btn.secondary{background:linear-gradient(180deg,#2b3548,#1d2535);border:1px solid #2a3648}
.btn.warn{background:linear-gradient(180deg,#6f6b2a,#5a501b)}
.kv{display:flex;gap:14px;flex-wrap:wrap;color:var(--dim);font-size:13px}
.kv b{color:var(--ink)} .count{font-weight:800;font-size:clamp(36px,12vw,64px);letter-spacing:.04em}
.badge{display:inline-block;border-radius:999px;padding:6px 10px;font-size:12px;border:1px solid #2a3648;color:var(--dim)}
.flex{display:flex;gap:12px;flex-wrap:wrap}
.toast{position:fixed;left:12px;right:12px;bottom:12px;padding:14px;border-radius:12px;background:#132a17;color:#d5ffd0;border:1px solid #265c2c;display:none}
.toast.err{background:#3a1114;color:#ffd7db;border-color:#7d262c}
.small{font-size:12px;color:var(--dim)}
.warn{border-color:#6a5b2a;color:#f1da9a}
.pill{padding:10px 14px;border-radius:999px;border:1px solid #2a3648;background:#0e1420}
#stopMap{height:260px;border-radius:12px;border:1px solid #2a3648;overflow:hidden}
.map-hint{margin-top:8px;color:var(--dim);font-size:12px}
.color-row{display:flex;gap:10px;align-items:center}
.color-chip{width:42px;height:36px;border-radius:10px;border:1px solid #2a3648}
.color-wrap{display:grid;grid-template-columns:auto 1fr;gap:10px;align-items:center}
</style></head><body>
<div class="wrap grid grid-2">
  <div class="card">
    <h1>WL Display – Setup</h1>
    <p class="lead">Verbinde das Gerät mit deinem WLAN und hinterlege RBL & API-Key.</p>
    <div class="grid" style="gap:12px">
      <div class="row"><label for="ssid">WLAN-SSID</label><input id="ssid" placeholder="MeinWLAN"></div>
      <div class="row"><label for="pwd">WLAN-Passwort</label><input id="pwd" type="password" placeholder="••••••••"></div>
      <div class="row"><label for="rbl">RBL</label><input id="rbl" inputmode="numeric" placeholder="z. B. 1184"></div>
      <div class="row"><label for="apikey">Wiener Linien API-Key</label><input id="apikey" placeholder="DEIN_API_KEY"></div>
      <div class="flex">
        <button class="btn" id="saveBtn">Speichern & neu starten</button>
        <button class="btn secondary" id="statusBtn">Status</button>
        <button class="btn warn" id="fsFormatBtn" title="Formatiert LittleFS! Vorsicht.">LittleFS formatieren</button>
      </div>
      <div class="kv"><span>IP: <b id="ip">-</b></span><span>RSSI: <b id="rssi">-</b></span>
        <span class="badge" id="wlstate">WLAN: unbekannt</span>
        <span class="badge warn" id="timesync">Zeit: unbekannt</span></div>
      <p class="small">Erreichbar über <code>http://wldisplay.local</code> oder die zugewiesene IP.</p>
    </div>
  </div>

  <div class="card">
    <h1>Live-Anzeige</h1>
    <p class="lead">Dieser Countdown spiegelt die 7-Segment Anzeige (MM:SS).</p>
    <div class="count" id="countMMSS" style="color:#eaf2ff">--:--</div>
    <div class="flex">
      <span class="badge" id="modeBadge">Modus: countdown</span>
      <span class="badge" id="nextSync">Sync in: -</span>
      <span class="badge" id="standbyBadge">Standby: aus</span>
    </div>
    <div class="row" style="margin-top:12px">
      <label for="log">Letzte WL-JSON-Antwort</label>
      <textarea id="log" readonly placeholder="Noch keine Daten …"></textarea>
    </div>
  </div>

  <div class="card">
    <h1>Haltestellenkarte</h1>
    <p class="lead">Position laut API (<span id="coordLbl">–</span>)</p>
    <div id="stopMap"></div>
    <div class="map-hint">Quelle: © OpenStreetMap-Mitwirkende • Kachel-Server über OpenStreetMap.org</div>
  </div>

  <div class="card">
    <h1>LED & Farben</h1>
    <p class="lead">Leistung und Farben abhängig von der verbleibenden Zeit.</p>
    <div class="flex" style="align-items:center;flex-wrap:wrap">
      <button class="btn" id="powerBtn">LED: an</button>
      <button class="btn warn" id="standbyBtn">Standby: aus</button>
      <div class="pill">Helligkeit: <b id="brightVal">0</b></div>
    </div>
    <input id="brightRange" type="range" min="0" max="255" step="1" value="40" />
    <div class="row-2" style="margin-top:10px">
      <div class="row">
        <label>Rot unter (Sekunden)</label>
        <input id="tLow" type="number" min="0" max="3600" step="1" placeholder="30">
      </div>
      <div class="row">
        <label>Gelb unter (Sekunden)</label>
        <input id="tMid" type="number" min="0" max="3600" step="1" placeholder="90">
      </div>
    </div>

    <div class="row-2" style="margin-top:10px">
      <div class="color-wrap">
        <div class="color-chip" id="chipLow"></div>
        <div class="row">
          <label>Farbe: Rot-Bereich</label>
          <input id="cLow" type="color" value="#ff0000">
        </div>
      </div>

      <div class="color-wrap">
        <div class="color-chip" id="chipMid"></div>
        <div class="row">
          <label>Farbe: Gelb-Bereich</label>
          <input id="cMid" type="color" value="#ffb400">
        </div>
      </div>
    </div>

    <div class="color-wrap" style="margin-top:10px">
      <div class="color-chip" id="chipHigh"></div>
      <div class="row" style="margin:0">
        <label>Farbe: Grün-Bereich</label>
        <input id="cHigh" type="color" value="#00ff00">
      </div>
    </div>

    <div class="flex" style="margin-top:12px">
      <button class="btn" id="ledSave">LED-Einstellungen speichern</button>
      <span class="badge" id="previewInfo">Vorschau entspricht Live-Countdown</span>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
(() => {
  const $ = s => document.querySelector(s);
  const toast = (m, ok=true) => { const t=$('#toast'); t.textContent=m; t.className='toast'+(ok?'':' err'); t.style.display='block'; setTimeout(()=>t.style.display='none', 3500); };
  const hex2rgb = h => { const x=h.replace('#',''); return [parseInt(x.slice(0,2),16),parseInt(x.slice(2,4),16),parseInt(x.slice(4,6),16)]; };
  const rgb2hex = (r,g,b) => '#'+[r,g,b].map(v=>('0'+v.toString(16)).slice(-2)).join('');
  const fmt = s => (String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0'));

  // ---- State ----
  let secondsRemaining = 0, mode = 'countdown', syncCountdown = 0, timeSynced = false;
  let standby = false;

  // Farbe-Chips
  const setChip = (el, hex) => { el.style.background = hex; };

  // Leaflet Map
  let map, marker, haveMap=false;
  function ensureMap(lat, lon){
    if(!haveMap){
      map = L.map('stopMap', {zoomControl:true, attributionControl:true});
      L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        maxZoom: 19,
        attribution: '&copy; OpenStreetMap'
      }).addTo(map);
      haveMap = true;
    }
    const ll = [lat, lon];
    if(!marker){
      marker = L.marker(ll).addTo(map);
    }else{
      marker.setLatLng(ll);
    }
    map.setView(ll, 16);
  }

  function setPowerBtn(v){ $('#powerBtn').dataset.state = v ? 'on':'off'; $('#powerBtn').textContent = 'LED: ' + (v?'an':'aus'); }
  function setStandbyBtn(v){ standby=v; $('#standbyBtn').textContent = 'Standby: ' + (v?'an':'aus'); $('#standbyBadge').textContent='Standby: '+(v?'an':'aus'); }
  function setCountdownColorFromServer(col){
    if(!col || col.length<3){ $('#countMMSS').style.color=''; return; }
    $('#countMMSS').style.color = rgb2hex(col[0], col[1], col[2]);
  }

  // Setup speichern
  $('#saveBtn').addEventListener('click', async ()=>{
    const ssid=$('#ssid').value.trim(), password=$('#pwd').value, rbl=$('#rbl').value.trim(), apiKey=$('#apikey').value.trim();
    if(!ssid||!rbl||!apiKey){ toast('Bitte SSID, RBL und API-Key ausfüllen', false); return; }
    try{
      const res = await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password,rbl,apiKey})});
      if(res.ok){ toast('Gespeichert. Neustart…'); setTimeout(()=>location.reload(), 3000); } else { toast('Fehler beim Speichern ('+res.status+')', false); }
    }catch(e){ toast('Netzwerkfehler: '+e, false); }
  });
  $('#statusBtn').addEventListener('click', ()=>location.href='/api/status');

  // LittleFS formatieren
  $('#fsFormatBtn').addEventListener('click', async ()=>{
    const conf = prompt('SCHREIBE "FORMAT" um LittleFS zu formatieren (alle gespeicherten Daten gehen verloren).');
    if (conf !== 'FORMAT') { toast('Abgebrochen'); return; }
    try{
      const r = await fetch('/api/fs-format', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({confirm:'FORMAT'})});
      if(!r.ok) throw new Error(r.status);
      const j = await r.json();
      if(j.ok){ toast('LittleFS formatiert. Neustart …'); setTimeout(()=>location.reload(), 2500); }
      else toast('Fehler: '+(j.error||'unbekannt'), false);
    }catch(e){ toast('Netzwerkfehler: '+e, false); }
  });

  // LED Controls
  async function postLed(payload){
    try{
      const r = await fetch('/api/led',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
      if(!r.ok) throw new Error(r.status);
      toast('LED-Settings übernommen');
    }catch(e){ toast('Fehler: '+e, false); }
  }
  $('#powerBtn').addEventListener('click', ()=>{
    const on = $('#powerBtn').dataset.state!=='off';
    postLed({
      power: !on,
      brightness: +$('#brightRange').value,
      thresholds:{low:+$('#tLow').value, mid:+$('#tMid').value},
      colors:{low:hex2rgb($('#cLow').value), mid:hex2rgb($('#cMid').value), high:hex2rgb($('#cHigh').value)}
    });
  });
  $('#standbyBtn').addEventListener('click', async ()=>{
    try{
      const r = await fetch('/api/standby',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({standby: !standby})});
      if(!r.ok) throw new Error(r.status);
      const j = await r.json(); setStandbyBtn(!!j.standby);
      toast('Standby '+(j.standby?'aktiviert':'deaktiviert'));
    }catch(e){ toast('Fehler: '+e, false); }
  });

  $('#brightRange').addEventListener('input', (e)=>$('#brightVal').textContent = e.target.value);
  $('#brightRange').addEventListener('change', (e)=>postLed({brightness:+e.target.value}));

  const updateChips = ()=>{
    setChip($('#chipLow'),  $('#cLow').value);
    setChip($('#chipMid'),  $('#cMid').value);
    setChip($('#chipHigh'), $('#cHigh').value);
  };
  ['cLow','cMid','cHigh'].forEach(id=>$('#'+id).addEventListener('input', updateChips));
  updateChips();

  $('#ledSave').addEventListener('click', ()=>{
    postLed({
      power: $('#powerBtn').dataset.state!=='off',
      brightness: +$('#brightRange').value,
      thresholds:{low:+$('#tLow').value, mid:+$('#tMid').value},
      colors:{low:hex2rgb($('#cLow').value), mid:hex2rgb($('#cMid').value), high:hex2rgb($('#cHigh').value)}
    });
  });

  // Status holen & UI updaten
  async function refreshStatus(){
    try{
      const r = await fetch('/api/status',{cache:'no-store'});
      if(!r.ok) throw new Error(r.status);
      const s = await r.json();

      // WLAN / Zeit
      $('#ip').textContent = s.ip || '-';
      $('#rssi').textContent = (s.rssi!==undefined)? s.rssi+' dBm' : '-';
      $('#wlstate').textContent = 'WLAN: ' + (s.ip ? 'verbunden' : 'getrennt');
      $('#wlstate').style.borderColor = s.ip ? 'rgba(122,217,107,.6)' : '#7d262c';
      timeSynced = !!s.timeSynced;
      $('#timesync').textContent = 'Zeit: ' + (timeSynced ? 'synchron' : 'noch nicht synchron');
      $('#timesync').className = 'badge ' + (timeSynced ? '' : 'warn');

      // Countdown / Modus / Standby
      mode = s.mode || 'countdown';
      $('#modeBadge').textContent = 'Modus: ' + mode;
      secondsRemaining = timeSynced ? (s.secondsToBus || 0) : 0;
      setStandbyBtn(!!s.standby);

      // LED-Settings
      setPowerBtn(!!s.ledPower);
      $('#brightRange').value = s.brightness ?? 40;
      $('#brightVal').textContent = $('#brightRange').value;

      if (s.thresholds){
        $('#tLow').value = s.thresholds.low ?? 30;
        $('#tMid').value = s.thresholds.mid ?? 90;
      }
      if (s.colors){
        if (s.colors.low)  $('#cLow').value  = rgb2hex(s.colors.low[0],  s.colors.low[1],  s.colors.low[2]);
        if (s.colors.mid)  $('#cMid').value  = rgb2hex(s.colors.mid[0],  s.colors.mid[1],  s.colors.mid[2]);
        if (s.colors.high) $('#cHigh').value = rgb2hex(s.colors.high[0], s.colors.high[1], s.colors.high[2]);
        updateChips();
      }

      // Karte
      if (s.coords && typeof s.coords.lat === 'number' && typeof s.coords.lon === 'number'){
        $('#coordLbl').textContent = s.coords.lat.toFixed(6)+', '+s.coords.lon.toFixed(6);
        ensureMap(s.coords.lat, s.coords.lon);
      } else {
        $('#coordLbl').textContent = '—';
      }

      // JSON-Log
      try{ const l = await fetch('/api/last-payload',{cache:'no-store'}); if(l.ok){ $('#log').value = await l.text(); } }catch(_){}

      syncCountdown = timeSynced ? 5 : 1;
      setCountdownColorFromServer(s.currentColor);
    }catch(e){ /* AP-Modus ok */ }
  }

  // Sekundentick
  setInterval(()=>{
    if(mode==='off' || !timeSynced || standby){ $('#countMMSS').textContent='--:--'; return; }
    if(secondsRemaining>0) secondsRemaining--;
    $('#countMMSS').textContent = fmt(secondsRemaining);
    if(syncCountdown>0) syncCountdown--;
    $('#nextSync').textContent = 'Sync in: ' + (syncCountdown>0? syncCountdown+'s':'—');
  }, 1000);

  // Status polling
  setInterval(refreshStatus, 1000);
  refreshStatus();
})();
</script>
</body></html>
)HTML";

// ---------- Hilfen ----------
void logLine(const String &s) {
  Serial.println(s);
  ringLog += s; ringLog += "\n";
  if (ringLog.length() > 6000)
    ringLog.remove(0, ringLog.length() - 6000);
}
bool fsInfo(size_t &total, size_t &used) {
  FSInfo i;
  if (!LittleFS.info(i)) return false;
  total = i.totalBytes;
  used  = i.usedBytes;
  return true;
}

// ISO-8601 → epoch (UTC) inkl. Offset (+hhmm / Z)
time_t parseISO8601_UTC_to_epoch(const String& iso){
  int Y=iso.substring(0,4).toInt();
  int M=iso.substring(5,7).toInt();
  int D=iso.substring(8,10).toInt();
  int h=iso.substring(11,13).toInt();
  int m=iso.substring(14,16).toInt();
  int s=iso.substring(17,19).toInt();

  int offSign=0, offH=0, offMin=0;
  if (iso.indexOf('Z', 19) >= 0) { offSign=0; offH=0; offMin=0; }
  else {
    int p = iso.indexOf('+', 19); if (p<0) p = iso.indexOf('-', 19);
    if (p>=0){ offSign = (iso.charAt(p)=='+') ? +1 : -1;
      offH = iso.substring(p+1, p+3).toInt();
      offMin = iso.substring(p+3, p+5).toInt();
    }
  }
  tm t = {}; t.tm_year=Y-1900; t.tm_mon=M-1; t.tm_mday=D; t.tm_hour=h; t.tm_min=m; t.tm_sec=s;
  char *oldtz = getenv("TZ"); setenv("TZ","UTC0",1); tzset();
  time_t epoch = mktime(&t);
  if (oldtz) setenv("TZ", oldtz, 1); else unsetenv("TZ"); tzset();
  epoch -= offSign * (offH*3600 + offMin*60);
  return epoch;
}

// Quotestring nach Needle
static bool scanQuotedAfter(const String& src, int from, const char* needle, String& out, int &posOut){
  int k = src.indexOf(needle, from);
  if (k < 0) return false;
  int sidx = k + strlen(needle);
  int eidx = src.indexOf('"', sidx);
  if (eidx < 0) return false;
  out = src.substring(sidx, eidx);
  posOut = eidx + 1;
  return true;
}

// Ganzzahl nach Needle (z.B. "countdown":123)
static bool scanIntAfter(const String& src, int from, const char* needle, int &value, int &posOut){
  int k = src.indexOf(needle, from);
  if (k < 0) return false;
  int sidx = k + strlen(needle);
  while (sidx < (int)src.length() && (src[sidx]==' ' || src[sidx]=='\t' || src[sidx]==':')) sidx++;
  int e = sidx; bool neg=false;
  if (e<(int)src.length() && (src[e]=='-'||src[e]=='+')) { neg = (src[e]=='-'); e++; }
  long val=0; bool any=false;
  while (e<(int)src.length() && isDigit(src[e])) { val = val*10 + (src[e]-'0'); e++; any=true; }
  if(!any) return false;
  value = neg ? -(int)val : (int)val;
  posOut = e;
  return true;
}

// Koordinaten aus Payload holen: "coordinates":[lon,lat]
static bool scanCoordinates(const String& src, double &lon, double &lat){
  int k = src.indexOf("\"coordinates\":[");
  if (k < 0) return false;
  int sidx = k + 15; // hinter [
  // einfachen Parser bauen
  // lese erstes Double (lon)
  int i=sidx; bool seen=false;
  String a="", b="";
  // Skip Spaces
  while (i<(int)src.length() && (src[i]==' '||src[i]=='\t')) i++;
  // lon
  while (i<(int)src.length() && (isDigit(src[i]) || src[i]=='-' || src[i]=='+' || src[i]=='.' || src[i]=='e' || src[i]=='E')){
    a += src[i++];
    seen=true;
  }
  if(!seen) return false;
  // Komma
  while (i<(int)src.length() && (src[i]==' '||src[i]=='\t')) i++;
  if (i>=(int)src.length() || src[i]!=',') return false;
  i++;
  // lat
  while (i<(int)src.length() && (src[i]==' '||src[i]=='\t')) i++;
  seen=false;
  while (i<(int)src.length() && (isDigit(src[i]) || src[i]=='-' || src[i]=='+' || src[i]=='.' || src[i]=='e' || src[i]=='E')){
    b += src[i++];
    seen=true;
  }
  if(!seen) return false;
  lon = a.toFloat();
  lat = b.toFloat();
  return true;
}

// ---------- Anzeige-Helfer ----------
void displayClear(){ strip.clear(); strip.show(); }
void displayMinusBoth(uint8_t r,uint8_t g,uint8_t b){
  strip.clear();
  for(int d=0; d<4; d++) for(int l=0;l<2;l++){ int idx = pgm_read_byte(&digitSegmentMap[d][6*2+l]); strip.setPixelColor(idx, strip.Color(r,g,b)); }
  strip.show(); lastDisplayValue="--";
}
void displayBlinkMinus(bool st,uint8_t r,uint8_t g,uint8_t b){
  strip.clear(); int d= st?0:1; for(int l=0;l<2;l++){ int idx = pgm_read_byte(&digitSegmentMap[d][6*2+l]); strip.setPixelColor(idx, strip.Color(r,g,b)); }
  strip.show(); lastDisplayValue="--";
}
void displayDigitsMMSS(int seconds,uint8_t r,uint8_t g,uint8_t b){
  int min = seconds/60; if(min<0) min=0; int sec = seconds%60; if(sec<0) sec=0;
  char buf[6]; sprintf(buf,"%02d:%02d",min,sec); lastDisplayValue = String(buf);
  int dval[4] = { min/10, min%10, sec/10, sec%10 };
  strip.clear();
  for (int d=0; d<4; d++){
    uint8_t val=dval[d]; if(val>9) continue;
    for(int s=0;s<7;s++){ bool on = pgm_read_byte(&digitPatterns[val][s]); if(!on) continue;
      for(int l=0;l<2;l++){ int idx = pgm_read_byte(&digitSegmentMap[d][s*2+l]); strip.setPixelColor(idx, strip.Color(r,g,b)); } }
  }
  strip.show();
}
void pickColorForSeconds(int secs, uint8_t &r,uint8_t &g,uint8_t &b){
  uint16_t a = min(cfg.tLow, cfg.tMid);
  uint16_t bnd = max(cfg.tLow, cfg.tMid);
  if(secs < a){ r=cfg.colLow[0]; g=cfg.colLow[1]; b=cfg.colLow[2]; }
  else if(secs < bnd){ r=cfg.colMid[0]; g=cfg.colMid[1]; b=cfg.colMid[2]; }
  else { r=cfg.colHigh[0]; g=cfg.colHigh[1]; b=cfg.colHigh[2]; }
}

// ---------- FS Helpers ----------
bool ensureFS() {
  if (fsMounted) return true;
  fsMounted = LittleFS.begin();
  if (!fsMounted) {
    Serial.println("LittleFS.begin() failed");
  }
  return fsMounted;
}

// ---------- Config speichern/lesen ----------
bool saveConfig(){
  if(!ensureFS()){
    Serial.println("saveConfig: FS not mounted");
    return false;
  }
  StaticJsonDocument<640> doc;
  doc["ssid"]=cfg.ssid; doc["password"]=cfg.password; doc["apiKey"]=cfg.apiKey; doc["rbl"]=cfg.rbl;
  doc["brightness"]=cfg.brightness; doc["ledPower"]=cfg.ledPower;
  doc["tLow"]=cfg.tLow; doc["tMid"]=cfg.tMid;
  JsonArray low = doc.createNestedArray("colLow");  low.add(cfg.colLow[0]);  low.add(cfg.colLow[1]);  low.add(cfg.colLow[2]);
  JsonArray mid = doc.createNestedArray("colMid");  mid.add(cfg.colMid[0]);  mid.add(cfg.colMid[1]);  mid.add(cfg.colMid[2]);
  JsonArray hig = doc.createNestedArray("colHigh"); hig.add(cfg.colHigh[0]); hig.add(cfg.colHigh[1]); hig.add(cfg.colHigh[2]);
  doc["httpsInsecure"]=cfg.httpsInsecure; doc["httpsFingerprint"]=cfg.httpsFingerprint;

  String json; json.reserve(768);
  size_t want = serializeJson(doc, json);
  if (want == 0){ Serial.println("saveConfig: serializeJson produced 0B"); return false; }

  auto writeTmp = [&](const char* name)->size_t{
    File f = LittleFS.open(name, "w");
    if(!f){ Serial.printf("saveConfig: open(%s) failed\n", name); return 0; }
    size_t n = f.write((const uint8_t*)json.c_str(), json.length());
    f.flush();
    f.close();
    return n;
  };

  size_t written = writeTmp("/config.tmp");
  Serial.printf("saveConfig: geplant=%uB, geschrieben=%uB (try#1)\n", (unsigned)want, (unsigned)written);

  if (written == 0){
    size_t tot=0, used=0;
    if (fsInfo(tot,used)) Serial.printf("FS vor Format: used=%u / total=%u\n", (unsigned)used, (unsigned)tot);
    LittleFS.end(); delay(25);
    LittleFS.format(); delay(25);
    fsMounted = LittleFS.begin();
    if (!fsMounted){ Serial.println("saveConfig: mount after format failed"); return false; }
    written = writeTmp("/config.tmp");
    Serial.printf("saveConfig: geschrieben=%uB (try#2)\n", (unsigned)written);
  }

  if (written == 0){
    Serial.println("saveConfig: both writes were 0B → abort");
    LittleFS.remove("/config.tmp");
    return false;
  }

  File v = LittleFS.open("/config.tmp","r");
  if(!v){ Serial.println("saveConfig: verify open failed"); LittleFS.remove("/config.tmp"); return false; }
  size_t vsz = v.size();
  String vstr = v.readString();
  v.close();
  if (vsz == 0 || vstr.length() == 0){
    Serial.println("saveConfig: verify read is 0B → abort");
    LittleFS.remove("/config.tmp");
    return false;
  }

  LittleFS.remove("/config.json");
  if(!LittleFS.rename("/config.tmp","/config.json")){
    Serial.println("saveConfig: rename tmp→json failed");
    LittleFS.remove("/config.tmp");
    return false;
  }

  File r = LittleFS.open("/config.json","r");
  if(!r){
    Serial.println("saveConfig: /config.json not readable after rename");
    return false;
  }
  size_t sz = r.size();
  String prev = r.readString();
  r.close();

  Serial.printf("---LittleFS gespeichert--- %u Bytes\n", (unsigned)sz);
  Serial.print("config.json preview: ");
  Serial.println(prev);

  delay(50);
  return (sz > 0);
}

void loadConfig(){
  if(!ensureFS()){
    Serial.println("loadConfig: FS not mounted");
    return;
  }
  if(!LittleFS.exists("/config.json")){
    Serial.println("---LittleFS: Noch keine Konfiguration gespeichert.---");
    return;
  }
  File f = LittleFS.open("/config.json","r");
  if(!f){ Serial.println("loadConfig: open failed"); return; }

  size_t sz = f.size();
  String raw = f.readString();
  f.close();

  Serial.printf("config.json size: %u Bytes\n", (unsigned)sz);
  Serial.print("config.json preview: ");
  Serial.println(raw);

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, raw);
  if(err){
    Serial.print("Config JSON parse failed: "); Serial.println(err.c_str());
    LittleFS.remove("/config.json");
    return;
  }

  cfg.ssid       = doc["ssid"]       | "";
  cfg.password   = doc["password"]   | "";
  cfg.apiKey     = doc["apiKey"]     | "";
  cfg.rbl        = doc["rbl"]        | "";
  cfg.brightness = doc["brightness"] | 40;
  cfg.ledPower   = doc["ledPower"]   | true;
  cfg.tLow       = doc["tLow"]       | 30;
  cfg.tMid       = doc["tMid"]       | 90;

  if (doc["colLow"].is<JsonArray>()){
    cfg.colLow[0]=doc["colLow"][0] | 255; cfg.colLow[1]=doc["colLow"][1] | 0;   cfg.colLow[2]=doc["colLow"][2] | 0;
  }
  if (doc["colMid"].is<JsonArray>()){
    cfg.colMid[0]=doc["colMid"][0] | 255; cfg.colMid[1]=doc["colMid"][1] | 180; cfg.colMid[2]=doc["colMid"][2] | 0;
  }
  if (doc["colHigh"].is<JsonArray>()){
    cfg.colHigh[0]=doc["colHigh"][0] | 0;   cfg.colHigh[1]=doc["colHigh"][1] | 255; cfg.colHigh[2]=doc["colHigh"][2] | 0;
  }

  cfg.httpsInsecure    = doc["httpsInsecure"]    | false;
  cfg.httpsFingerprint = doc["httpsFingerprint"] | "";

  Serial.println("---LittleFS Konfig geladen---");
}

// ---------- WiFi/NTP ----------
void ensureWifi(){
  if(WiFi.status()==WL_CONNECTED) return;
  WiFi.disconnect(); if(cfg.ssid.isEmpty()) return;
  logLine("Verbinde mit WLAN..."); WiFi.mode(WIFI_STA); WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  unsigned long t0=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000){ delay(250); yield(); Serial.print("."); } Serial.println();
  if(WiFi.status()==WL_CONNECTED){
    logLine("Verbunden! IP: "+WiFi.localIP().toString());
  } else logLine("WLAN Fehler!");
}
bool ensureTime() {
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  setenv("TZ", cfg.tz.c_str(), 1); tzset();
  for (int i=0; i<50; i++){
    time_t now = time(nullptr);
    if (now >= TIME_VALID_EPOCH) {
      timeSynced = true;
      logLine(String("Zeit synchron: ") + String(ctime(&now)).c_str());
      return true;
    }
    delay(200); yield();
  }
  timeSynced = (time(nullptr) >= TIME_VALID_EPOCH);
  return timeSynced;
}

// ---------- WL-Fetch ----------
int fetchBusCountdown(){
  if (standby) return -1;

  ensureWifi();
  if (WiFi.status()!=WL_CONNECTED) return -1;

  // Stempel (nur Log)
  time_t now=time(nullptr); struct tm *tm_ = localtime(&now);
  char tbuf[10]; sprintf(tbuf,"%02d:%02d:%02d",tm_->tm_hour,tm_->tm_min,tm_->tm_sec);

  String url = "https://www.wienerlinien.at/ogd_realtime/monitor?rbl="+cfg.rbl+"&activateTrafficInfo=stoerungkurz&sender="+cfg.apiKey;

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  if(cfg.httpsFingerprint.length()>0){ client->setFingerprint(cfg.httpsFingerprint.c_str()); logLine("TLS: Fingerprint-Pinning aktiv"); }
  else if(cfg.httpsInsecure){ client->setInsecure(); logLine("TLS: INSECURE (testweise)"); }
  else { client->setInsecure(); logLine("TLS: Fallback insecure (kein Fingerprint gesetzt)"); }

  HTTPClient http;
  logLine("["+String(tbuf)+"] GET "+url);
  if(!http.begin(*client, url)){ logLine("HTTP begin failed"); return -1; }

  int code=http.GET();
  String payload = (code>0)? http.getString() : "";
  http.end();

  logLine("HTTP Code: "+String(code));
  if(code!=HTTP_CODE_OK){
    if(payload.length()>0 && payload.length()<16384) lastPayload=payload;
    return -1;
  }
  if(payload.length()==0){
    logLine("HTTP 200, aber Body leer");
    return -1;
  }

  if(payload.length()>16384) payload.remove(16384);
  lastPayload = payload;

  // 1) serverTime
  String serverIso; int p=0;
  if(!scanQuotedAfter(payload, 0, "\"serverTime\":\"", serverIso, p)){
    logLine("serverTime nicht gefunden – Abbruch");
    return -1;
  }
  time_t tServer = parseISO8601_UTC_to_epoch(serverIso);
  Serial.println(String("serverTime: ")+serverIso+" → epoch="+String((uint32_t)tServer));
  timeSynced = true;

  // 1a) Koordinaten extrahieren (optional)
  double lon, lat;
  if (scanCoordinates(payload, lon, lat)) {
    stopLon = lon; stopLat = lat; haveCoords = true;
    Serial.println(String("coordinates: lon=")+String(lon,6)+", lat="+String(lat,6));
  }

  // 2) timeReal sammeln
  struct Hit { String iso; int countdown; };
  Hit hits[4]; int hitCount=0;
  int pos=0;
  while (hitCount < 4) {
    String iso; int nextPos=0;
    if(!scanQuotedAfter(payload, pos, "\"timeReal\":\"", iso, nextPos)) break;
    int cd= -1, dummy=0;
    int searchFrom = nextPos;
    int searchTo = min((int)payload.length(), searchFrom + 220);
    String window = payload.substring(searchFrom, searchTo);
    if(scanIntAfter(window, 0, "\"countdown\":", cd, dummy)){}
    hits[hitCount++] = { iso, cd };
    pos = nextPos;
  }
  if(hitCount==0){
    logLine("timeReal nicht gefunden – Abbruch");
    return -1;
  }

  auto evalHit = [&](const Hit& h, int idx)->int {
    time_t tDep = parseISO8601_UTC_to_epoch(h.iso);
    long diff = (long)tDep - (long)tServer;
    Serial.println(String("timeReal[")+idx+"]: "+h.iso+" → diff="+String(diff)+"s, fallbackCountdown="+String(h.countdown));
    if (diff >= 0) return (int)diff;
    if (h.countdown >= 0) {
      Serial.println(String("timeReal[")+idx+"] negativ → nutze countdown="+String(h.countdown));
      return h.countdown;
    }
    return -1;
  };

  int best = evalHit(hits[0], 0);
  if (best < 0 && hitCount >= 2) {
    Serial.println("timeReal[0] negativ → versuche timeReal[1] …");
    best = evalHit(hits[1], 1);
  }
  if (best < 0) {
    logLine("Weder timeReal[0] noch [1] ergeben gültigen Wert – Abbruch");
    return -1;
  }

  secondsToBus = best;
  lastUpdateMs = millis();

  int mm = secondsToBus/60, ss = secondsToBus%60; char mmss[6]; sprintf(mmss,"%02d:%02d",mm,ss);
  logLine(String("--- Countdown (serverTime vs timeReal): ")+String(secondsToBus)+" sec ("+mmss+") ---");
  return secondsToBus;
}

// ---------- Anzeige-States ----------
enum class DisplayMode : uint8_t { COUNTDOWN, OFF, MINUS, STANDBY };
DisplayMode mode = DisplayMode::COUNTDOWN;

void applyLedState(){
  strip.setBrightness(cfg.brightness);
  if(!cfg.ledPower || standby){ displayClear(); return; }
  uint8_t r,g,b; pickColorForSeconds(secondsToBus, r,g,b);
  switch(mode){
    case DisplayMode::COUNTDOWN: if(secondsToBus>0) displayDigitsMMSS(secondsToBus,r,g,b); else displayMinusBoth(r,g,b); break;
    case DisplayMode::OFF: displayClear(); break;
    case DisplayMode::MINUS: displayMinusBoth(r,g,b); break;
    case DisplayMode::STANDBY: displayClear(); break;
  }
}

// ---------- Web Handlers ----------
void serveIndex(){ server.send_P(200,"text/html; charset=utf-8", INDEX_HTML); }

void handleStatus(){
  StaticJsonDocument<1200> doc;
  doc["ip"] = (WiFi.status()==WL_CONNECTED)? WiFi.localIP().toString() : "";
  doc["rssi"] = (WiFi.status()==WL_CONNECTED)? WiFi.RSSI() : 0;
  doc["lastDisplay"] = lastDisplayValue;
  doc["secondsToBus"] = (timeSynced && !standby) ? secondsToBus : 0;
  doc["lastUpdateMs"] = lastUpdateMs;
  doc["timeSynced"] = timeSynced;
  doc["now"] = (uint32_t) time(nullptr);
  doc["standby"] = standby;

  doc["ledPower"] = cfg.ledPower;
  doc["brightness"] = cfg.brightness;

  uint8_t rr=255,gg=255,bb=255;
  pickColorForSeconds(secondsToBus, rr,gg,bb);
  JsonArray curr = doc["currentColor"].to<JsonArray>(); curr.add(rr); curr.add(gg); curr.add(bb);

  JsonObject thresholds = doc["thresholds"].to<JsonObject>(); thresholds["low"]=cfg.tLow; thresholds["mid"]=cfg.tMid;
  JsonObject colors = doc["colors"].to<JsonObject>();
  JsonArray low = colors["low"].to<JsonArray>();   low.add(cfg.colLow[0]);   low.add(cfg.colLow[1]);   low.add(cfg.colLow[2]);
  JsonArray mid = colors["mid"].to<JsonArray>();   mid.add(cfg.colMid[0]);   mid.add(cfg.colMid[1]);   mid.add(cfg.colMid[2]);
  JsonArray high= colors["high"].to<JsonArray>(); high.add(cfg.colHigh[0]); high.add(cfg.colHigh[1]); high.add(cfg.colHigh[2]);

  if (haveCoords) { JsonObject c = doc["coords"].to<JsonObject>(); c["lat"] = stopLat; c["lon"] = stopLon; }

  doc["mode"] = (mode==DisplayMode::COUNTDOWN ? "countdown" :
                (mode==DisplayMode::OFF ? "off" :
                (mode==DisplayMode::MINUS ? "minus" : "standby")));
  doc["ssid"] = cfg.ssid; doc["apiKey"] = cfg.apiKey; doc["rbl"] = cfg.rbl;
  doc["httpsInsecure"] = cfg.httpsInsecure; doc["httpsFingerprint"] = cfg.httpsFingerprint;
  doc["loglen"] = ringLog.length();
  String out; serializeJson(doc,out);
  server.send(200,"application/json", out);
}
void handleLastPayload(){ server.send(200, lastPayload.length()? "application/json":"text/plain", lastPayload); }

void handleConfigPost(){
  if(!server.hasArg("plain")){ server.send(400,"text/plain","Missing body"); return; }
  StaticJsonDocument<1024> doc;
  if(deserializeJson(doc, server.arg("plain"))){ server.send(400,"text/plain","Invalid JSON"); return; }
  if(doc["ssid"].is<const char*>())        cfg.ssid = doc["ssid"].as<const char*>();
  if(doc["password"].is<const char*>())    cfg.password = doc["password"].as<const char*>();
  if(doc["apiKey"].is<const char*>())      cfg.apiKey = doc["apiKey"].as<const char*>();
  if(doc["rbl"].is<const char*>())         cfg.rbl = doc["rbl"].as<const char*>();
  if(doc["httpsInsecure"].is<bool>())      cfg.httpsInsecure = doc["httpsInsecure"].as<bool>();
  if(doc["httpsFingerprint"].is<const char*>()) cfg.httpsFingerprint = doc["httpsFingerprint"].as<const char*>();

  Serial.println("---Werte aus Webformular übernommen---");
  Serial.println("SSID: " + cfg.ssid);
  Serial.println("Passwort: " + cfg.password);
  Serial.println("API-Key: " + cfg.apiKey);
  Serial.println("RBL: " + cfg.rbl);

  bool ok = saveConfig();
  if(!ok){
    server.send(500,"text/plain","Speichern fehlgeschlagen (siehe Serial-Log)");
    return;
  }
  server.send(200,"text/html","Daten gespeichert. Neustart…");
  delay(500);
  ESP.restart();
}

void handleLedPost(){
  if(!server.hasArg("plain")){ server.send(400,"text/plain","Missing body"); return; }
  StaticJsonDocument<1024> doc; if(deserializeJson(doc, server.arg("plain"))){ server.send(400,"text/plain","Invalid JSON"); return; }

  if(doc["power"].is<bool>()) cfg.ledPower = doc["power"].as<bool>();
  if(doc["brightness"].is<int>()) cfg.brightness = (uint8_t)constrain((int)doc["brightness"],0,255);

  if(doc["thresholds"].is<JsonObject>()){
    JsonObject t = doc["thresholds"].as<JsonObject>();
    if(t["low"].is<int>()) cfg.tLow = (uint16_t)constrain((int)t["low"],0,3600);
    if(t["mid"].is<int>()) cfg.tMid = (uint16_t)constrain((int)t["mid"],0,3600);
  }

  if(doc["colors"].is<JsonObject>()){
    JsonObject c = doc["colors"].as<JsonObject>();
    if(c["low"].is<JsonArray>()){
      cfg.colLow[0]=(uint8_t)constrain((int)c["low"][0],0,255);
      cfg.colLow[1]=(uint8_t)constrain((int)c["low"][1],0,255);
      cfg.colLow[2]=(uint8_t)constrain((int)c["low"][2],0,255);
    }
    if(c["mid"].is<JsonArray>()){
      cfg.colMid[0]=(uint8_t)constrain((int)c["mid"][0],0,255);
      cfg.colMid[1]=(uint8_t)constrain((int)c["mid"][1],0,255);
      cfg.colMid[2]=(uint8_t)constrain((int)c["mid"][2],0,255);
    }
    if(c["high"].is<JsonArray>()){
      cfg.colHigh[0]=(uint8_t)constrain((int)c["high"][0],0,255);
      cfg.colHigh[1]=(uint8_t)constrain((int)c["high"][1],0,255);
      cfg.colHigh[2]=(uint8_t)constrain((int)c["high"][2],0,255);
    }
  }

  saveConfig(); applyLedState(); server.send(200,"application/json","{\"ok\":true}");
}

void handleDisplayPost(){
  if(!server.hasArg("plain")){ server.send(400,"text/plain","Missing body"); return; }
  StaticJsonDocument<512> doc; if(deserializeJson(doc, server.arg("plain"))){ server.send(400,"text/plain","Invalid JSON"); return; }
  if(doc["mode"].is<const char*>()){
    String m=doc["mode"].as<const char*>();
    if(m=="countdown") mode=DisplayMode::COUNTDOWN;
    else if(m=="off") mode=DisplayMode::OFF;
    else if(m=="minus") mode=DisplayMode::MINUS;
    else if(m=="standby") { standby=true; mode=DisplayMode::STANDBY; }
  }
  saveConfig(); applyLedState(); server.send(200,"application/json","{\"ok\":true}");
}

void handleStandbyPost(){
  if(!server.hasArg("plain")){ server.send(400,"text/plain","Missing body"); return; }
  StaticJsonDocument<200> doc; if(deserializeJson(doc, server.arg("plain"))){ server.send(400,"text/plain","Invalid JSON"); return; }
  if(doc["standby"].is<bool>()){
    standby = doc["standby"].as<bool>();
    if(standby){ mode=DisplayMode::STANDBY; displayClear(); }
    else { if(mode==DisplayMode::STANDBY) mode=DisplayMode::COUNTDOWN; nextPollAt = millis()+500; }
  }
  String out = String("{\"ok\":true,\"standby\":") + (standby?"true":"false") + "}";
  server.send(200,"application/json", out);
}

void handleFetchNow(){
  int s=fetchBusCountdown(); if(s>=0){ secondsToBus=s; backoffMs=20000; }
  int mm = secondsToBus/60, ss = secondsToBus%60; char mmss[6]; sprintf(mmss,"%02d:%02d",mm,ss);
  String out=String("{\"ok\":")+(s>=0?"true":"false")+",\"seconds\":"+(s>=0?String(s):"-1")+",\"mmss\":\""+String(mmss)+"\"}";
  server.send(200,"application/json",out);
}

void handleFactoryReset(){ LittleFS.remove("/config.json"); server.send(200,"application/json","{\"ok\":true,\"note\":\"config removed; rebooting\"}"); delay(250); ESP.restart(); }

void handleFsFormat(){
  if(!server.hasArg("plain")){ server.send(400,"application/json","{\"ok\":false,\"error\":\"Missing body\"}"); return; }
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400,"application/json","{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  const char* confirm = doc["confirm"] | "";
  if (String(confirm) != "FORMAT") {
    server.send(403,"application/json","{\"ok\":false,\"error\":\"Confirmation token required\"}");
    return;
  }
  size_t total=0, used=0;
  if (fsInfo(total, used)) {
    Serial.println("FS-Format START: used="+String(used)+" / total="+String(total));
  }
  LittleFS.end();
  bool okFmt = LittleFS.format();
  bool okMount = LittleFS.begin();
  if (okFmt && okMount) {
    Serial.println("FS-Format OK → reboot");
    server.send(200,"application/json","{\"ok\":true}");
    delay(300);
    ESP.restart();
  } else {
    Serial.println(String("FS-Format FAILED: format=")+(okFmt?"true":"false")+", mount="+(okMount?"true":"false"));
    server.send(500,"application/json", String("{\"ok\":false,\"error\":\"format=") + (okFmt?"true":"false") + ", mount=" + (okMount?"true":"false") + "\"}");
  }
}

void addCORS(){ server.sendHeader("Access-Control-Allow-Origin","*"); server.sendHeader("Access-Control-Allow-Headers","Content-Type"); server.sendHeader("Access-Control-Allow-Methods","GET,POST,OPTIONS"); }

// ---------- Setup/Loop ----------
void setup(){
  Serial.begin(115200); delay(200); Serial.println(); Serial.println("Booting… F_CPU="+String(F_CPU));

  strip.begin(); strip.setBrightness(cfg.brightness); strip.show();
  ensureFS();
  loadConfig();

  if(cfg.ssid.isEmpty()){
    WiFi.mode(WIFI_AP); WiFi.softAP("WienerLinienDisplaySetup","12345678");
    logLine("Access Point: WienerLinienDisplaySetup / Passwort: 12345678");
    logLine("Web-Konfiguration auf http://192.168.4.1/");
  } else { WiFi.mode(WIFI_STA); ensureWifi(); }

  ensureTime();

  ArduinoOTA.setHostname("wldisplay"); ArduinoOTA.begin(); logLine("OTA bereit (Hostname: wldisplay)");
  if(MDNS.begin("wldisplay")) logLine("mDNS aktiv: http://wldisplay.local");

  // Routen
  server.on("/", HTTP_GET, [](){ addCORS(); serveIndex(); });
  server.on("/api/status", HTTP_GET, [](){ addCORS(); handleStatus(); });
  server.on("/api/last-payload", HTTP_GET, [](){ addCORS(); handleLastPayload(); });

  server.on("/api/config", HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/api/led",    HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/api/display",HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/api/fetch-now",HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/api/factoryreset",HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/api/standby",HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/api/fs-format",HTTP_OPTIONS, [](){ addCORS(); server.send(204); });

  server.on("/api/config", HTTP_POST, [](){ addCORS(); handleConfigPost(); });
  server.on("/api/led", HTTP_POST, [](){ addCORS(); handleLedPost(); });
  server.on("/api/display", HTTP_POST, [](){ addCORS(); handleDisplayPost(); });
  server.on("/api/fetch-now", HTTP_POST, [](){ addCORS(); handleFetchNow(); });
  server.on("/api/factoryreset", HTTP_POST, [](){ addCORS(); handleFactoryReset(); });
  server.on("/api/standby", HTTP_POST, [](){ addCORS(); handleStandbyPost(); });
  server.on("/api/fs-format", HTTP_POST, [](){ addCORS(); handleFsFormat(); });

  server.on("/status-log", HTTP_GET, [](){ addCORS(); server.send(200,"text/plain", ringLog); });

  server.begin(); logLine("HTTP server started");

  applyLedState();
  nextTickAt = millis() + 1000;
  nextPollAt = millis() + 500;
}

void loop(){
  server.handleClient();
  ArduinoOTA.handle();

  // 1s LED/Anzeige tick
  if(millis() >= nextTickAt){
    nextTickAt += 1000;
    if(cfg.ledPower && !standby){
      uint8_t r,g,b; pickColorForSeconds(secondsToBus, r,g,b);
      if(mode==DisplayMode::COUNTDOWN){
        if(secondsToBus>0){ secondsToBus--; displayDigitsMMSS(secondsToBus,r,g,b); }
        else { blinkState=!blinkState; displayBlinkMinus(blinkState,r,g,b); }
      } else if(mode==DisplayMode::MINUS){
        blinkState=!blinkState; displayBlinkMinus(blinkState,r,g,b);
      } else displayClear();
    } else displayClear();
  }

  // Poll WL (Erfolg 20s, Fehler 5s)
  if(!standby && millis() >= nextPollAt){
    int s = fetchBusCountdown();
    if (s >= 0) { secondsToBus = s; backoffMs = 20000; }
    else { backoffMs = 5000; }
    nextPollAt = millis() + backoffMs;

    int mm = secondsToBus/60, ss = secondsToBus%60; char mmss[6]; sprintf(mmss,"%02d:%02d",mm,ss);
    Serial.println("Next poll in " + String(backoffMs) + " ms; seconds=" + String(secondsToBus) + " (" + String(mmss) + ")");
  }

  yield();
}
