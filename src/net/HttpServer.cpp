#include "HttpServer.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../core/AppState.h"
#include "../core/ConfigStore.h"
#include "../core/Settings.h"
#include "../core/firmware_version.h"
#include "WifiManager.h"

namespace {
constexpr uint16_t HTTP_PORT = 80;
constexpr uint8_t  DNS_PORT  = 53;

// JSON string escaper. Output is appended to `out`.
void appendJsonString(String& out, const char* s) {
    out += '"';
    if (s) {
        for (const char* p = s; *p; ++p) {
            unsigned char c = static_cast<unsigned char>(*p);
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20) {
                        char esc[8];
                        snprintf(esc, sizeof(esc), "\\u%04x", c);
                        out += esc;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
    }
    out += '"';
}

void sendJsonError(WebServer* s, int code, const char* msg) {
    String out = "{\"ok\":false,\"error\":";
    appendJsonString(out, msg);
    out += '}';
    s->send(code, "application/json", out);
}

void sendJsonOk(WebServer* s) {
    s->send(200, "application/json", "{\"ok\":true}");
}

// Build the /api/networks JSON. Triggers a scan if none has run yet.
String buildNetworksJson(WifiManager& wifi) {
    bool running = wifi.scanRunning();
    int  count   = wifi.scanResultCount();
    if (!running && count < 0) {
        wifi.startScan();
        running = true;
    }
    String out;
    out.reserve(512);
    out += "{\"scanning\":";
    out += running ? "true" : "false";
    out += ",\"networks\":[";
    if (!running && count > 0) {
        for (int i = 0; i < count; ++i) {
            if (i) out += ',';
            out += "{\"ssid\":";
            appendJsonString(out, WiFi.SSID(i).c_str());
            out += ",\"rssi\":";
            out += String(WiFi.RSSI(i));
            out += ",\"secure\":";
            out += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "false" : "true";
            out += '}';
        }
    }
    out += "]}";
    return out;
}

void handleSaveNetwork(WebServer* s, ConfigStore& cfg) {
    if (!s->hasArg("ssid") || s->arg("ssid").length() == 0) {
        sendJsonError(s, 400, "missing ssid");
        return;
    }
    String ssid = s->arg("ssid");
    String pass = s->hasArg("pass") ? s->arg("pass") : "";
    if (!cfg.setCreds(ssid.c_str(), pass.c_str())) {
        sendJsonError(s, 400, "invalid ssid or password length");
        return;
    }
    s->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(500);
    ESP.restart();
}

}  // namespace

HttpServer::HttpServer(WifiManager& wifi, const AppState& app,
                       ConfigStore& config, Settings& settings)
    : wifi_(wifi), app_(app), config_(config), settings_(settings),
      server_(nullptr), dns_(nullptr), role_(Role::NONE), boot_ms_(0) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::begin() {
    boot_ms_ = millis();
}

void HttpServer::tick(uint32_t /*now_ms*/) {
    Role want = Role::NONE;
    switch (wifi_.state()) {
        case WifiState::STA_CONNECTED:   want = Role::STA; break;
        case WifiState::AP_PROVISIONING: want = Role::AP;  break;
        default: want = Role::NONE; break;
    }

    if (want != role_) {
        stop();
        if (want == Role::STA) startSta();
        else if (want == Role::AP) startAp();
    }

    if (server_) server_->handleClient();
    if (dns_)    dns_->processNextRequest();
}

void HttpServer::startSta() {
    server_ = new WebServer(HTTP_PORT);
    registerStaHandlers();
    server_->begin();
    Serial.printf("[http] STA listening on http://%s\n",
                  WiFi.localIP().toString().c_str());
    role_ = Role::STA;
}

void HttpServer::startAp() {
    server_ = new WebServer(HTTP_PORT);
    registerApHandlers();
    server_->begin();

    dns_ = new DNSServer();
    dns_->setErrorReplyCode(DNSReplyCode::NoError);
    dns_->start(DNS_PORT, "*", WiFi.softAPIP());

    Serial.printf("[http] AP listening on http://%s\n",
                  WiFi.softAPIP().toString().c_str());
    role_ = Role::AP;
}

void HttpServer::stop() {
    if (server_) {
        server_->stop();
        delete server_;
        server_ = nullptr;
    }
    if (dns_) {
        dns_->stop();
        delete dns_;
        dns_ = nullptr;
    }
    role_ = Role::NONE;
}

// ---- STA-mode dashboard + APIs ---------------------------------------------

void HttpServer::registerStaHandlers() {
    server_->on("/", HTTP_GET, [this]() {
        const settings::Settings& sd = settings_.data();
        String html;
        html.reserve(8192);
        html += F(
            "<!doctype html><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>");
        html += sd.device_name;
        html += F(
            "</title>"
            "<style>"
            ":root{background-color:#E5E7DF;--border:#DCDED4;--container:#F3F4F0;"
            "--button-border:#CCCCCC;--radius:8px;--input:#FFF;--panel:#FFF;"
            "--label:#a0a0a0;--button-shadow:#E1DDDD;--tip:#343434;"
            "--link:rgb(245 78 0);--ok:#1a7f37;--err:#c93232;"
            "font-family:system-ui,-apple-system,sans-serif}"
            "body{margin:0 auto;padding:1rem;background:var(--container);color:#333;"
            "max-width:70ch;border:1px solid var(--border)}"
            "h1{font-weight:700;font-size:1.5rem;margin:0 0 .25rem;color:#000}"
            "h2{font-weight:600;font-size:1.1rem;margin:0 0 .5rem;color:#000}"
            ".sub{color:var(--tip);margin:0 0 1.25rem;font-size:.95rem}"
            ".section{background:var(--panel);padding:1rem;border-radius:var(--radius);"
            "margin-bottom:1rem;border:1px solid var(--border)}"
            ".form-group{margin-bottom:1rem}"
            "label{display:block;margin-bottom:.4rem;font-weight:600;font-size:.95rem}"
            "input,select,button{font:inherit;width:100%;height:40px;font-size:16px;"
            "padding:0 12px;border:1px solid var(--button-border);border-radius:var(--radius);"
            "background:var(--input);box-sizing:border-box;color:inherit;"
            "-webkit-tap-highlight-color:transparent}"
            "button{font-weight:600;cursor:pointer;"
            "box-shadow:0 4px 0 0 var(--button-shadow),0 5px 0 0 var(--button-border)}"
            "button.danger{background:#fff;color:var(--err);border-color:#e3b3b3}"
            "button:active{background:#DCDDD9;transform:translateY(2px);"
            "box-shadow:0 2px 0 0 var(--button-shadow),0 3px 0 0 var(--button-border)}"
            ".row{display:flex;gap:.5rem;align-items:center}.row>*{flex:1}"
            ".row .small{flex:0 0 auto;width:auto;padding:0 14px}"
            ".tip{font-weight:400;font-size:.85rem;color:var(--tip);margin:.4rem 0 0}"
            ".kv{display:grid;grid-template-columns:max-content 1fr;gap:.4rem 1rem;"
            "font-size:.95rem}.kv .k{color:var(--label);font-weight:600;font-size:.85rem;"
            "text-transform:uppercase;letter-spacing:.04em}.kv .v{font-feature-settings:'tnum'}"
            ".badge{display:inline-block;padding:2px 10px;border-radius:99px;"
            "font-size:.75rem;font-weight:700;color:#fff;background:#999;letter-spacing:.04em;"
            "vertical-align:middle;margin-left:.5rem}.badge.live{background:var(--ok)}"
            ".badge.offln{background:var(--err)}"
            ".msg{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;"
            "color:#444;background:#FAFAF7;border-radius:6px;padding:.6rem .8rem;"
            "word-break:break-word;font-size:.9rem;min-height:1em}"
            ".cards-list{list-style:none;padding:0;margin:0}"
            ".cards-list li{display:flex;align-items:center;gap:.5rem;padding:.4rem 0;"
            "border-bottom:1px solid #F0F0EC}"
            ".cards-list li:last-child{border-bottom:0}"
            ".cards-list .name{flex:1;font-weight:600}"
            ".cards-list .arrow{flex:0 0 auto;width:36px;height:36px;padding:0;font-size:18px}"
            ".cards-list input[type=checkbox]{flex:0 0 auto;width:auto;height:auto;"
            "transform:scale(1.3);margin-right:.4rem}"
            ".status-msg{font-size:.85rem;margin-top:.5rem;min-height:1.2em}"
            ".status-msg.ok{color:var(--ok)}.status-msg.err{color:var(--err)}"
            "footer{color:#999;font-size:.8rem;text-align:center;margin-top:1rem}"
            "a{color:var(--link);font-weight:600;text-decoration:none}"
            "a:hover{text-decoration:underline}"
            "</style>"
            "<h1 id=hname>");
        html += sd.device_name;
        html += F("<span id=live class='badge offln'>OFFLN</span></h1>"
                  "<p class=sub><span id=ssid>");
        html += wifi_.ssid();
        html += F("</span> &middot; <span id=ip>");
        html += wifi_.ip().toString();
        html += F("</span></p>"

            // -------- STATUS --------
            "<div class=section>"
              "<h2>Status</h2>"
              "<div class=kv>"
                "<span class=k>state</span><span class=v id=state>"
                  "&mdash;</span>"
                "<span class=k>total</span><span class=v id=total>0</span>"
                "<span class=k>running</span><span class=v id=running>0</span>"
                "<span class=k>waiting</span><span class=v id=waiting>0</span>"
              "</div>"
              "<div class=k style='margin:.8rem 0 .3rem'>last msg</div>"
              "<div id=msg class=msg>&mdash;</div>"
            "</div>"

            // -------- DEVICE --------
            "<div class=section>"
              "<h2>Device</h2>"
              "<form id=device-form onsubmit='return saveDevice(event)'>"
                "<div class=form-group>"
                  "<label for=dn>Name</label>"
                  "<input id=dn name=name maxlength=15 required pattern='[ -~]+'>"
                  "<p class=tip>1-15 ASCII characters. Used for BLE and the footer.</p>"
                "</div>"
                "<div class=form-group>"
                  "<label for=lt>Live timeout (s)</label>"
                  "<input id=lt name=live_timeout_s type=number min=5 max=300 required>"
                  "<p class=tip>How long without a snapshot before we show OFFLN. 5..300.</p>"
                "</div>"
                "<div class=form-group>"
                  "<label for=st>Sleep timeout (s)</label>"
                  "<input id=st name=sleep_timeout_s type=number min=0 max=3600 required>"
                  "<p class=tip>Backlight off after N seconds idle. 0 disables. Otherwise 30..3600.</p>"
                "</div>"
                "<div class=form-group>"
                  "<label for=dt>Dim timeout (s)</label>"
                  "<input id=dt name=dim_timeout_s type=number min=0 max=3600 required>"
                  "<p class=tip>Backlight dims after N seconds idle. 0 disables. Otherwise 5..3600. Must be less than sleep timeout.</p>"
                "</div>"
                "<div class=form-group>"
                  "<label for=dl>Dim level (%)</label>"
                  "<input id=dl name=dim_level_pct type=number min=1 max=99 required>"
                  "<p class=tip>Brightness while dimmed. 1..99.</p>"
                "</div>"
                "<div class=form-group>"
                  "<label for=fl>Full level (%)</label>"
                  "<input id=fl name=full_level_pct type=number min=1 max=100 required>"
                  "<p class=tip>Brightness while active. 1..100.</p>"
                "</div>"
                "<button type=submit>Save device</button>"
                "<p id=device-msg class=status-msg></p>"
              "</form>"
            "</div>"

            // -------- CARDS --------
            "<div class=section>"
              "<h2>Cards</h2>"
              "<form id=cards-form onsubmit='return saveCards(event)'>"
                "<ul class=cards-list id=cards-ul></ul>"
                "<div class=form-group style='margin-top:1rem'>"
                  "<label for=bc>Boot card</label>"
                  "<select id=bc name=boot_card></select>"
                  "<p class=tip>Which card is shown first on boot.</p>"
                "</div>"
                "<button type=submit>Save cards</button>"
                "<p id=cards-msg class=status-msg></p>"
              "</form>"
            "</div>"

            // -------- NETWORK --------
            "<div class=section>"
              "<h2>Network</h2>"
              "<form id=wifi-form onsubmit='return saveWifi(event)'>"
                "<div class=form-group>"
                  "<label for=net>Wi-Fi network</label>"
                  "<div class=row>"
                    "<select id=net name=ssid required></select>"
                    "<button type=button class=small id=rescan title=Rescan>&#x21bb;</button>"
                  "</div>"
                "</div>"
                "<div class=form-group>"
                  "<label for=pw>Password</label>"
                  "<input id=pw name=pass type=password maxlength=64 autocomplete=off>"
                  "<p class=tip>Leave blank if the network is open. Saving reboots the device.</p>"
                "</div>"
                "<button type=submit>Save and reboot</button>"
                "<p id=wifi-msg class=status-msg></p>"
              "</form>"
            "</div>"

            // -------- DANGER --------
            "<div class=section>"
              "<h2>Danger zone</h2>"
              "<div class=row style='gap:.6rem;flex-wrap:wrap'>"
                "<button class=danger type=button onclick=\"act('reboot','Rebooting...')\">Reboot</button>"
                "<button class=danger type=button onclick=\"act('reset-settings','Resetting settings...')\">Reset settings</button>"
                "<button class=danger type=button onclick=\"act('forget-wifi','Forgetting Wi-Fi...')\">Forget Wi-Fi</button>"
              "</div>"
              "<p id=danger-msg class=status-msg></p>"
            "</div>"

            "<footer>uptime <span id=uptime>0</span>s &middot; "
              "<a href=/api/status>raw status</a> &middot; "
              "<a href=/api/settings>raw settings</a></footer>"

            "<script>"
            "const $=id=>document.getElementById(id);"
            "function setMsg(el,txt,cls){el.textContent=txt;el.className='status-msg '+(cls||'')}"
            "async function jget(p){const r=await fetch(p);return r.json()}"
            "async function jpost(p,body){"
              "const opts={method:'POST'};"
              "if(body)opts.body=body;"
              "const r=await fetch(p,opts);"
              "let j={};try{j=await r.json()}catch(e){}"
              "return{ok:r.ok&&j.ok!==false,j,status:r.status}"
            "}"

            "async function pollStatus(){"
              "try{"
                "const j=await jget('/api/status');"
                "$('state').textContent=j.claude_state;"
                "$('total').textContent=j.status.total;"
                "$('running').textContent=j.status.running;"
                "$('waiting').textContent=j.status.waiting;"
                "$('msg').textContent=j.status.msg||'\\u2014';"
                "$('uptime').textContent=Math.round(j.uptime_ms/1000);"
                "const b=$('live');b.className='badge '+(j.live?'live':'offln');"
                "b.textContent=j.live?'LIVE':'OFFLN';"
                "$('ssid').textContent=j.ssid;$('ip').textContent=j.ip;"
              "}catch(e){}"
            "}"

            "let cardCatalog=[];"
            "async function loadSettings(){"
              "const s=await jget('/api/settings');"
              "if($('hname').firstChild)$('hname').firstChild.nodeValue=s.device_name;"
              "$('dn').value=s.device_name;"
              "$('lt').value=s.live_timeout_s;"
              "$('st').value=s.sleep_timeout_s;"
              "$('dt').value=s.dim_timeout_s;"
              "$('dl').value=s.dim_level_pct;"
              "$('fl').value=s.full_level_pct;"
              "cardCatalog=s.cards.slice().sort((a,b)=>("
                "(a.order==null?99:a.order)-(b.order==null?99:b.order)"
              "));"
              "renderCards();renderBoot();"
              "if(typeof s.boot_card_id==='number')$('bc').value=s.boot_card_id"
            "}"
            "function renderCards(){"
              "const ul=$('cards-ul');ul.innerHTML='';"
              "cardCatalog.forEach((c,idx)=>{"
                "const li=document.createElement('li');"
                "li.dataset.id=c.id;"
                "li.innerHTML="
                  "'<input type=checkbox '+(c.enabled?'checked':'')+'>'+"
                  "'<span class=name>'+c.name+'</span>'+"
                  "'<button type=button class=arrow data-dir=-1>&#x2191;</button>'+"
                  "'<button type=button class=arrow data-dir=1>&#x2193;</button>';"
                "li.querySelector('input').onchange=ev=>{"
                  "c.enabled=ev.target.checked;renderBoot();"
                "};"
                "li.querySelectorAll('button').forEach(btn=>btn.onclick=ev=>{"
                  "const dir=parseInt(ev.target.dataset.dir,10);"
                  "const j=idx+dir;"
                  "if(j<0||j>=cardCatalog.length)return;"
                  "[cardCatalog[idx],cardCatalog[j]]=[cardCatalog[j],cardCatalog[idx]];"
                  "renderCards();renderBoot();"
                "});"
                "ul.appendChild(li)"
              "})"
            "}"
            "function renderBoot(){"
              "const sel=$('bc');const prev=sel.value;sel.innerHTML='';"
              "cardCatalog.filter(c=>c.enabled).forEach(c=>{"
                "const o=document.createElement('option');"
                "o.value=c.id;o.textContent=c.name;sel.appendChild(o)"
              "});"
              "if(prev&&[...sel.options].some(o=>o.value==prev))sel.value=prev"
            "}"

            "async function saveDevice(ev){"
              "ev.preventDefault();"
              "const f=new FormData($('device-form'));"
              "const{ok,j}=await jpost('/api/settings/device',f);"
              "setMsg($('device-msg'),ok?'saved.':(j.error||'failed.'),ok?'ok':'err');"
              "if(ok)await loadSettings();"
              "return false"
            "}"
            "async function saveCards(ev){"
              "ev.preventDefault();"
              "let mask=0;const order=[];"
              "cardCatalog.forEach(c=>{if(c.enabled){mask|=1<<c.id;order.push(c.id)}});"
              "const boot=parseInt($('bc').value,10);"
              "const fd=new FormData();fd.append('enabled_mask',mask);"
              "order.forEach(id=>fd.append('order',id));"
              "fd.append('boot_card',boot);"
              "const{ok,j}=await jpost('/api/settings/cards',fd);"
              "setMsg($('cards-msg'),ok?'saved.':(j.error||'failed.'),ok?'ok':'err');"
              "if(ok)await loadSettings();"
              "return false"
            "}"

            "async function loadNets(){"
              "const sel=$('net');sel.innerHTML='<option value=\"\">scanning...</option>';"
              "try{"
                "const j=await jget('/api/networks');"
                "if(j.scanning){setTimeout(loadNets,1500);return}"
                "const list=(j.networks||[]).slice().sort((a,b)=>b.rssi-a.rssi);"
                "sel.innerHTML='<option value=\"\">Select a network</option>';"
                "list.forEach(n=>{"
                  "const o=document.createElement('option');"
                  "o.value=n.ssid;"
                  "o.textContent=n.ssid+' ('+n.rssi+'dBm'+(n.secure?'':', open')+')';"
                  "sel.appendChild(o)"
                "});"
                "if(!list.length)sel.innerHTML='<option value=\"\">no networks found</option>'"
              "}catch(e){sel.innerHTML='<option value=\"\">scan failed</option>'}"
            "}"
            "$('rescan').onclick=async()=>{"
              "$('rescan').disabled=true;setMsg($('wifi-msg'),'rescanning...');"
              "try{await jpost('/api/scan')}catch(e){}"
              "setTimeout(()=>{$('rescan').disabled=false;loadNets();setMsg($('wifi-msg'),'')},2200)"
            "};"
            "async function saveWifi(ev){"
              "ev.preventDefault();"
              "const f=new FormData($('wifi-form'));"
              "setMsg($('wifi-msg'),'saving and rebooting...');"
              "const{ok,j}=await jpost('/api/settings/network',f);"
              "if(!ok)setMsg($('wifi-msg'),j.error||'failed.','err');"
              "return false"
            "}"

            "async function act(name,label){"
              "if(!confirm('Confirm: '+label))return;"
              "setMsg($('danger-msg'),label);"
              "const{ok,j}=await jpost('/api/actions/'+name);"
              "if(!ok)setMsg($('danger-msg'),j.error||'failed.','err')"
            "}"

            "loadSettings();loadNets();pollStatus();setInterval(pollStatus,3000);"
            "</script>");
        server_->send(200, "text/html", html);
    });

    // ---- /api/firmware-version
    server_->on("/api/firmware-version", HTTP_GET, [this]() {
        String out = "{\"version\":";
        appendJsonString(out, FIRMWARE_VERSION);
        out += '}';
        server_->send(200, "application/json", out);
    });

    // ---- /api/status (kept under /api/* now)
    server_->on("/api/status", HTTP_GET, [this]() {
        const ClaudeStatus& s = app_.status();
        const uint32_t now = millis();
        String out;
        out.reserve(384);
        out += "{\"state\":";        appendJsonString(out, wifi_.stateName());
        out += ",\"ssid\":";          appendJsonString(out, wifi_.ssid());
        out += ",\"ip\":\"";          out += wifi_.ip().toString(); out += '"';
        out += ",\"uptime_ms\":";     out += String(now - boot_ms_);
        out += ",\"device\":";        appendJsonString(out, app_.deviceName());
        out += ",\"live\":";          out += app_.isLive(now) ? "true" : "false";
        out += ",\"claude_state\":";  appendJsonString(out, state_name(app_.buddyState()));
        out += ",\"status\":{\"total\":"; out += s.total;
        out += ",\"running\":";       out += s.running;
        out += ",\"waiting\":";       out += s.waiting;
        out += ",\"msg\":";           appendJsonString(out, s.msg);
        out += "}}";
        server_->send(200, "application/json", out);
    });

    // ---- /api/settings (read)
    server_->on("/api/settings", HTTP_GET, [this]() {
        char buf[768];
        size_t n = settings::toJson(settings_.data(), buf, sizeof(buf));
        if (n == 0) {
            sendJsonError(server_, 500, "settings JSON overflow");
            return;
        }
        server_->send(200, "application/json", buf);
    });

    // ---- /api/settings/device
    server_->on("/api/settings/device", HTTP_POST, [this]() {
        if (!server_->hasArg("name") || !server_->hasArg("live_timeout_s") ||
            !server_->hasArg("sleep_timeout_s") ||
            !server_->hasArg("dim_timeout_s") ||
            !server_->hasArg("dim_level_pct") ||
            !server_->hasArg("full_level_pct")) {
            sendJsonError(server_, 400, "missing field");
            return;
        }
        String name = server_->arg("name");
        long lt = server_->arg("live_timeout_s").toInt();
        long st = server_->arg("sleep_timeout_s").toInt();
        long dt = server_->arg("dim_timeout_s").toInt();
        long dl = server_->arg("dim_level_pct").toInt();
        long fl = server_->arg("full_level_pct").toInt();
        if (lt < 0 || lt > 0xFFFF || st < 0 || st > 0xFFFF ||
            dt < 0 || dt > 0xFFFF ||
            dl < 0 || dl > 0xFF ||
            fl < 0 || fl > 0xFF) {
            sendJsonError(server_, 400, "field out of range");
            return;
        }
        char err[64] = {};
        if (!settings_.applyDevice(name.c_str(),
                                   static_cast<uint16_t>(lt),
                                   static_cast<uint16_t>(st),
                                   err, sizeof(err))) {
            sendJsonError(server_, 400, err);
            return;
        }
        if (!settings_.applyBacklight(static_cast<uint16_t>(dt),
                                      static_cast<uint8_t>(dl),
                                      static_cast<uint8_t>(fl),
                                      err, sizeof(err))) {
            sendJsonError(server_, 400, err);
            return;
        }
        sendJsonOk(server_);
    });

    // ---- /api/settings/cards
    server_->on("/api/settings/cards", HTTP_POST, [this]() {
        if (!server_->hasArg("enabled_mask") || !server_->hasArg("boot_card")) {
            sendJsonError(server_, 400, "missing field");
            return;
        }
        long mask_long = server_->arg("enabled_mask").toInt();
        long boot_long = server_->arg("boot_card").toInt();
        if (mask_long < 0 || mask_long > 0xFF || boot_long < 0 || boot_long > 0xFF) {
            sendJsonError(server_, 400, "field out of range");
            return;
        }

        // Repeated 'order' fields each become a separate args() entry;
        // walk the full list to collect them in submit order.
        uint8_t order[settings::CARD_COUNT] = {0};
        uint8_t count = 0;
        for (int i = 0; i < server_->args() && count < settings::CARD_COUNT; ++i) {
            if (server_->argName(i) != "order") continue;
            long v = server_->arg(i).toInt();
            if (v < 0 || v >= settings::CARD_COUNT) {
                sendJsonError(server_, 400, "order has bad card id");
                return;
            }
            order[count++] = (uint8_t)v;
        }

        char err[64] = {};
        if (!settings_.applyCards((uint8_t)mask_long, order, count,
                                  (uint8_t)boot_long, err, sizeof(err))) {
            sendJsonError(server_, 400, err);
            return;
        }
        sendJsonOk(server_);
    });

    // ---- /api/settings/network (also reachable in AP mode)
    server_->on("/api/settings/network", HTTP_POST, [this]() {
        handleSaveNetwork(server_, config_);
    });

    // ---- /api/scan + /api/networks
    server_->on("/api/scan", HTTP_POST, [this]() {
        wifi_.startScan();
        server_->send(202, "application/json", "{\"ok\":true,\"scanning\":true}");
    });
    server_->on("/api/networks", HTTP_GET, [this]() {
        server_->send(200, "application/json", buildNetworksJson(wifi_));
    });

    // ---- /api/actions/*
    server_->on("/api/actions/reboot", HTTP_POST, [this]() {
        server_->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
        delay(500);
        ESP.restart();
    });
    server_->on("/api/actions/reset-settings", HTTP_POST, [this]() {
        settings_.clearToDefaults(app_.macDeviceName());
        server_->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
        delay(500);
        ESP.restart();
    });
    server_->on("/api/actions/forget-wifi", HTTP_POST, [this]() {
        config_.clear();
        server_->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
        delay(500);
        ESP.restart();
    });

    // ---- legacy redirects
    auto redirectTo = [this](const char* loc) {
        server_->sendHeader("Location", loc, true);
        server_->send(301, "text/plain", "");
    };
    server_->on("/status",    HTTP_GET,  [this, redirectTo]() { redirectTo("/api/status"); });
    server_->on("/networks",  HTTP_GET,  [this, redirectTo]() { redirectTo("/api/networks"); });
    server_->on("/scan",      HTTP_POST, [this, redirectTo]() { redirectTo("/api/scan"); });
    server_->on("/save",      HTTP_POST, [this, redirectTo]() { redirectTo("/api/settings/network"); });

    server_->onNotFound([this]() {
        server_->send(404, "text/plain", "not found");
    });
}

// ---- AP-mode captive portal ------------------------------------------------

void HttpServer::registerApHandlers() {
    server_->on("/", HTTP_GET, [this]() {
        static const char kPortalHtml[] PROGMEM =
            "<!doctype html><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>claude-buddy setup</title>"
            "<style>"
            ":root{background-color:#E5E7DF;--border:#DCDED4;--container:#F3F4F0;"
            "--button-border:#CCCCCC;--radius:8px;--input:#FFF;--panel:#FFF;"
            "--button-shadow:#E1DDDD;--tip:#343434;--err:#c93232;"
            "font-family:system-ui,-apple-system,sans-serif}"
            "body{margin:0 auto;padding:1rem;background:var(--container);color:#333;"
            "max-width:60ch;border:1px solid var(--border)}"
            "h1{font-size:1.5rem;margin:0 0 .5rem;color:#000}"
            ".section{background:var(--panel);padding:1rem;border-radius:var(--radius);"
            "margin-bottom:1rem;border:1px solid var(--border)}"
            "label{display:block;margin:.6rem 0 .3rem;font-weight:600}"
            "input,select,button{width:100%;height:40px;font:inherit;font-size:16px;"
            "padding:0 12px;border:1px solid var(--button-border);border-radius:var(--radius);"
            "background:var(--input);box-sizing:border-box}"
            "button{font-weight:600;cursor:pointer;margin-top:1rem;"
            "box-shadow:0 4px 0 0 var(--button-shadow),0 5px 0 0 var(--button-border)}"
            "button:active{transform:translateY(2px);"
            "box-shadow:0 2px 0 0 var(--button-shadow),0 3px 0 0 var(--button-border)}"
            ".tip{font-size:.85rem;color:var(--tip);margin:.4rem 0 0}"
            ".row{display:flex;gap:.5rem}.row select{flex:1}.row .small{flex:0 0 auto;width:auto;padding:0 14px}"
            ".msg{font-size:.85rem;margin-top:.5rem;min-height:1.2em;color:var(--err)}"
            "</style>"
            "<h1>claude-buddy setup</h1>"
            "<div class=section>"
              "<form id=f method=POST action=/api/settings/network onsubmit='return go(event)'>"
                "<label for=ssid>Wi-Fi network</label>"
                "<div class=row>"
                  "<select id=ssid name=ssid required></select>"
                  "<button type=button class=small id=rescan title=Rescan>&#x21bb;</button>"
                "</div>"
                "<label for=pass>Password</label>"
                "<input id=pass name=pass type=password maxlength=64 autocomplete=off>"
                "<p class=tip>Leave blank if the network is open.</p>"
                "<button type=submit>Save and join</button>"
                "<p id=msg class=msg></p>"
              "</form>"
            "</div>"
            "<script>"
            "const $=id=>document.getElementById(id);"
            "async function load(){"
              "const sel=$('ssid');sel.innerHTML='<option value=\"\">scanning...</option>';"
              "try{"
                "const r=await fetch('/api/networks');const j=await r.json();"
                "if(j.scanning){setTimeout(load,1500);return}"
                "const list=(j.networks||[]).slice().sort((a,b)=>b.rssi-a.rssi);"
                "sel.innerHTML='<option value=\"\">Select a network</option>';"
                "list.forEach(n=>{"
                  "const o=document.createElement('option');"
                  "o.value=n.ssid;"
                  "o.textContent=n.ssid+' ('+n.rssi+'dBm'+(n.secure?'':', open')+')';"
                  "sel.appendChild(o)"
                "});"
                "if(!list.length)sel.innerHTML='<option value=\"\">no networks found</option>'"
              "}catch(e){sel.innerHTML='<option value=\"\">scan failed</option>'}"
            "}"
            "$('rescan').onclick=async()=>{"
              "$('rescan').disabled=true;"
              "try{await fetch('/api/scan',{method:'POST'})}catch(e){}"
              "setTimeout(()=>{$('rescan').disabled=false;load()},2200)"
            "};"
            "async function go(ev){"
              "ev.preventDefault();$('msg').textContent='saving and rebooting...';"
              "const r=await fetch('/api/settings/network',"
                "{method:'POST',body:new FormData($('f'))});"
              "let j={};try{j=await r.json()}catch(e){}"
              "if(!r.ok||j.ok===false)$('msg').textContent=(j.error||'failed.');"
              "return false"
            "}"
            "load();"
            "</script>";
        server_->send_P(200, "text/html", kPortalHtml);
    });

    server_->on("/api/networks", HTTP_GET, [this]() {
        server_->send(200, "application/json", buildNetworksJson(wifi_));
    });
    server_->on("/api/scan", HTTP_POST, [this]() {
        wifi_.startScan();
        server_->send(202, "application/json", "{\"ok\":true,\"scanning\":true}");
    });
    server_->on("/api/settings/network", HTTP_POST, [this]() {
        handleSaveNetwork(server_, config_);
    });

    // Legacy redirects for older captive-portal clients.
    auto redirectTo = [this](const char* loc) {
        server_->sendHeader("Location", loc, true);
        server_->send(301, "text/plain", "");
    };
    server_->on("/networks", HTTP_GET,  [this, redirectTo]() { redirectTo("/api/networks"); });
    server_->on("/scan",     HTTP_POST, [this, redirectTo]() { redirectTo("/api/scan"); });
    server_->on("/save",     HTTP_POST, [this, redirectTo]() { redirectTo("/api/settings/network"); });

    // Captive-portal probe URLs from common clients — redirect to /.
    auto redirectRoot = [this]() {
        server_->sendHeader("Location", "http://192.168.4.1/", true);
        server_->send(302, "text/plain", "");
    };
    server_->on("/generate_204",        HTTP_GET, redirectRoot);  // Android
    server_->on("/hotspot-detect.html", HTTP_GET, redirectRoot);  // iOS / macOS
    server_->on("/connecttest.txt",     HTTP_GET, redirectRoot);  // Windows
    server_->on("/redirect",            HTTP_GET, redirectRoot);
    server_->on("/ncsi.txt",            HTTP_GET, redirectRoot);

    server_->onNotFound(redirectRoot);
}
