// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "captive_portal_html_fragments.h"

// ── HTML / CSS / JS fragments ───────────────────────────────────────────────
//
// Kept as named raw-string literals so the markup stays readable without
// `\"` escapes everywhere. The fragments with `%s` placeholders are fed
// through snprintf at render time; ones without placeholders are streamed
// directly via httpd_resp_send_chunk(.., HTTPD_RESP_USE_STRLEN).
//
// IMPORTANT: do not put bare `%` characters in these literals — the
// fragments with format specifiers go through snprintf, so any stray `%`
// would be interpreted as a conversion. The CSS uses `100%` only in the
// HEAD fragment, which is *not* fed through snprintf, so it's safe there.

// Shared form styling — used by both /config and /appliance via the head
// helpers below. Keeping a single source of truth avoids the trap of
// rendering different-looking inputs across pages just because one HEAD
// forgot a selector (which happened with the appliance text inputs).



// language=CSS
const char CAPTIVE_HTML_COMMON_STYLE[] = R"HTML(<style>
body {
    font-family: system-ui, sans-serif;
    max-width: 480px;
    margin: 1em auto;
    padding: 0 1em;
    color: #222;
}
h1 {
    font-size: 1.2em;
}
fieldset {
    border: 1px solid #ccc;
    border-radius: 6px;
    margin: 1em 0;
    padding: .5em 1em;
}
legend {
    padding: 0 .5em;
    color: #555;
    font-size: .9em;
}
label {
    display: block;
    margin: .6em 0 .2em;
    font-size: .9em;
}
input[type=text], input[type=password], select {
    width: 100%;
    padding: .5em;
    font-size: 1em;
    border: 1px solid #bbb;
    border-radius: 4px;
    box-sizing: border-box;
}
input:disabled {
    background: #eee;
    color: #888;
}
textarea#appliance-config {
    width: 100%;
    min-height: 12em;
    padding: .5em;
    font-size: .85em;
    font-family: monospace;
    border: 1px solid #bbb;
    border-radius: 4px;
    box-sizing: border-box;
    resize: none;
    overflow: hidden;
}
textarea#tls_pem {
    width: 100%;
    min-height: 12em;
    padding: .5em;
    font-size: .85em;
    font-family: monospace;
    border: 1px solid #bbb;
    border-radius: 4px;
    box-sizing: border-box;
    resize: none;
}
button {
    margin-top: 1em;
    padding: .7em 1.2em;
    font-size: 1em;
    background: #0a7;
    color: #fff;
    border: 0;
    border-radius: 4px;
    cursor: pointer;
}
.hint {
    font-size: .8em;
    color: #666;
    margin: .2em 0 0;
}
.nav {
    margin: 0 0 1em;
    font-size: .9em;
}
.nav a {
    color: #0a7;
    text-decoration: none;
}
</style>)HTML";



// language=HTML
const char CAPTIVE_HTML_HEAD_OPEN[] = R"HTML(<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>S2 Modbus Converter</title>
)HTML";



// language=HTML
const char CAPTIVE_HTML_HEAD_CLOSE_CONFIG[] = R"HTML(</head>
<body>
<h1>S2 Modbus Converter — config</h1>
<p class="nav"><a href="/appliance">→ Appliance config (JSON upload)</a></p>
<form method="POST" action="/config">
)HTML";



// Placeholders (in order):
//   %s = pre-filled SSID (already HTML-escaped).
//   %s = " checked" if the WiFi STA netif should override Ethernet, else "".
// language=HTML
const char CAPTIVE_HTML_FIELDSET_WIFI[] = R"HTML(<fieldset>
    <legend>WiFi (client)</legend>
    <label>SSID
        <input type="text" name="wifi_ssid" value="%s" maxlength="32">
    </label>
    <label>Password
        <input type="password" name="wifi_password" placeholder="●●●●●●●●" maxlength="64">
    </label>
    <p class="hint">Leave password empty to keep the stored one.</p>
    <label>
        <input type="checkbox" name="prefer_wifi" value="1" %s>
        Use WiFi interface instead of Ethernet for MODBUS appliance (less secure!)
    </label>
    <p class="hint">When unchecked: Ethernet is used when a cable is plugged in, WiFi otherwise.</p>
</fieldset>
)HTML";



// Placeholders (in order):
//   %s = pre-filled websocket URI (already HTML-escaped).
//   %s = " checked" if TLS is enabled, else "".
//   %s = " disabled" if TLS is off (greys out PEM textarea), else "".
//   %s = pre-filled PEM (already HTML-escaped); may be empty.
// language=HTML
const char CAPTIVE_HTML_FIELDSET_S2[] = R"HTML(<fieldset>
    <legend>S2 / Websocket</legend>
    <label>Endpoint URI
        <input type="text" name="websocket_uri" value="%s" maxlength="128">
    </label>
    <label>Pair key
        <input type="text" name="pair_key" placeholder="XXXXXXX" maxlength="128">
    </label>
    <p class="hint">You can get a pair key from the S2 CEM/HEMS</p>
    <label>
        <input type="checkbox" name="tls_enabled" value="1" %s
               onchange="document.getElementsByName('tls_pem')[0].disabled=!this.checked">
        Use TLS 
    </label>
    <label>CA certificate (PEM)
        <textarea name="tls_pem" id="tls_pem" rows="8" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----" maxlength="4096" %s>%s</textarea>
    </label>
</fieldset>
)HTML";



// Placeholders (in order):
//   %s = " checked" if static IP is enabled, else "".
//   %s/%s/%s/%s/%s/%s = three pairs of (escaped value, " disabled"-or-empty)
// for the three address inputs.
// language=HTML
const char CAPTIVE_HTML_FIELDSET_NETWORK[] = R"HTML(<fieldset>
    <legend>Network</legend>
    <label>
        <input type="checkbox" name="ip_static" value="1" %s
               onchange="['ip_addr','ip_netmask','ip_gateway'].forEach(function(k){document.getElementsByName(k)[0].disabled=!this.checked}.bind(this))">
        Use static IP (otherwise DHCP)
    </label>
    <label>IP address
        <input type="text" name="ip_addr" value="%s" maxlength="15" %s>
    </label>
    <label>Netmask
        <input type="text" name="ip_netmask" value="%s" maxlength="15" %s>
    </label>
    <label>Gateway
        <input type="text" name="ip_gateway" value="%s" maxlength="15" %s>
    </label>
</fieldset>
)HTML";



// language=HTML
const char CAPTIVE_HTML_TAIL[] = R"HTML(<button type="submit">Save &amp; reboot</button>
</form>
</body>
</html>
)HTML";




// ── /appliance upload page ──────────────────────────────────────────────────
//
// Split into HEAD (contains '%' in CSS — must NOT go through snprintf) and a
// small middle fragment that carries the "%u bytes stored" counter via
// snprintf. Same rationale as the /config page above: stray '%' would be
// interpreted as a conversion specifier.

// language=HTML
const char CAPTIVE_HTML_HEAD_CLOSE_APPLIANCE[] = R"HTML(</head>
<body>
<h1>Appliance config</h1>
<p class="nav"><a href="/config">← Back to device config</a></p>
<form method="POST" action="/appliance">
<fieldset>
    <legend>JSON config</legend>
    <script>
        // language=JavaScript

        function minify(t) {
            try {
                JSON.parse(t.value);  // validity check only
            }
            catch (e) {
                alert("Ongeldige JSON: " + e.message);
                return false;
            }
            let s = t.value;
            let out = "";
            let inStr = false;
            let esc = false;
            for (let i = 0; i < s.length; i++) {
                const c = s[i];
                if (inStr) {
                    out += c;
                    if (esc) {
                        esc = false;
                    }
                    else if (c === "\\") {
                        esc = true;
                    }
                    else if (c === '"') {
                        inStr = false;
                    }
                }
                else if (c === '"') {
                    inStr = true;
                    out += c;
                }
                else if (c === " " || c === "\t" || c === "\n" || c === "\r") {
                    // whitespace buiten strings overslaan
                }
                else {
                    out += c;
                }
            }
            if (out.length > 4096) {
                alert("JSON te groot: " + out.length + " bytes na minify (max 4096).");
                return false;
            }
            t.value = out;
            return true;
        }

        function prettify() {
            const t = document.getElementById("appliance-config");
            if (t.value.trim() === "") {
                return;
            }
            try {
                const x = JSON.parse(t.value);
                t.value = JSON.stringify(x, null, 2);
            }
            catch (e) {
                // Invalid JSON: leave the user's text untouched so they can fix it.
            }
        }

        function minifiedLength() {
            const t = document.getElementById("appliance-config");
            let s = t.value;
            let out = "";
            let inStr = false;
            let esc = false;
            for (let i = 0; i < s.length; i++) {
                const c = s[i];
                if (inStr) {
                    out += c;
                    if (esc) {
                        esc = false;
                    }
                    else if (c === "\\") {
                        esc = true;
                    }
                    else if (c === '"') {
                        inStr = false;
                    }
                }
                else if (c === '"') {
                    inStr = true;
                    out += c;
                }
                else if (c === " " || c === "\t" || c === "\n" || c === "\r") {
                    // skip
                }
                else {
                    out += c;
                }
            }
            return out.length;
        }

        function setTextareaCounter() {
            const len = minifiedLength();
            const counter = document.getElementById("counter");
            counter.textContent = len + " / 4096 tekens";
            if (len > 4096) {
                counter.className = "danger";
            }
            else if (len > 3500) {
                counter.className = "warn";
            }
            else {
                counter.className = "";
            }
        }

        function autoResize() {
            const t = document.getElementById("appliance-config");
            const scrollY = window.scrollY;
            t.style.height = 'auto';
            t.style.height = t.scrollHeight + 'px';
            window.scrollTo(0, scrollY);
        }

        function updateVisibility() {
            const textarea = document.getElementById("appliance-config");

            let json;
            try {
                json = JSON.parse(textarea.value);
            }
            catch (error) {
                return;
            }

            const mappings = [
                { name: 'appliance_name',     path: ['appliance', 'spec', 'name', 'source'] },
                { name: 'appliance_model',    path: ['appliance', 'spec', 'model', 'source'] },
                { name: 'appliance_serial',   path: ['appliance', 'spec', 'serial_number', 'source'] },
                { name: 'appliance_firmware', path: ['appliance', 'spec', 'firmwareVersion', 'source'] },
                { name: 'modbus_channel',     path: ['appliance', 'connection', 'modbusChannel', 'source'] },
                { name: 'modbus_ip',          path: ['appliance', 'connection', 'eth', 'ipAddress', 'source'] },
                { name: 'modbus_gateway',     path: ['appliance', 'connection', 'eth', 'gateway', 'source'] },
                { name: 'modbus_netmask',     path: ['appliance', 'connection', 'eth', 'netmask', 'source'] },
                { name: 'modbus_unit_id',     path: ['appliance', 'connection', 'unitId', 'source'] },
                { name: 'modbus_port',        path: ['appliance', 'connection', 'eth', 'port', 'source'] },
                { name: 'num_phases',         path: ['appliance', 'control', 'numberOfPhases', 'source'] },
            ];

            for (const mapping of mappings) {
                const input = document.querySelector(`[name="${mapping.name}"]`);
                if (!input) {
                    continue;
                }

                let node = json;
                for (const key of mapping.path) {
                    if (node === undefined || node === null) {
                        node = undefined;
                        break;
                    }
                    node = node[key];
                }

                const visible = node === 'USER_INPUT';
                if (visible) {
                    input.closest('label').style.display = '';
                }
                else {
                    input.closest('label').style.display = 'none';
                }
            }
        }

        function afterTextareaUpdate() {
            prettify();
            setTextareaCounter();
            updateVisibility();
            autoResize();
        }

        document.addEventListener("DOMContentLoaded", function () {
            const f = document.querySelector("form");
            const t = document.getElementById("appliance-config");
            if (!f || !t) {
                console.error("Form or textarea not found");
                return;
            }
            f.addEventListener("submit", (e) => {
                if (!minify(t)) {
                    e.preventDefault();
                }
            });
            t.addEventListener("input", () => {
                afterTextareaUpdate();
            });

            prettify();
            afterTextareaUpdate();
        });
    </script>
)HTML";



// Placeholder: %u = number of bytes currently stored (0 if absent).
// language=HTML
const char CAPTIVE_HTML_APPLIANCE_COUNTER[] = R"HTML(<label>Currently stored: %u bytes</label>
)HTML";

// Split around the textarea body so the currently-stored JSON can be streamed
// in between (HTML-escaped) as the initial value. Keeping it as two literals
// avoids needing a %s in the tail — the stored config can be much larger than
// our snprintf chunk buffer in worst-case escape inflation.
//
// Open/close tags are kept tight against the textarea content (no inner
// whitespace) because <textarea> renders leading/trailing whitespace verbatim.
// language=HTML
const char CAPTIVE_HTML_APPLIANCE_TEXTAREA_OPEN[] =
    R"HTML(<label>JSON<textarea id="appliance-config" name="appliance_config" maxlength="10096" spellcheck="false">)HTML";

// language=HTML
const char CAPTIVE_HTML_APPLIANCE_TEXTAREA_CLOSE[] = R"HTML(</textarea></label>
<p class="hint"><span id="counter">0 / 4096 characters</span></p>
</fieldset>
)HTML";



// Appliance parameter fieldset rendered below the JSON textarea. Placeholders
// (in order):
//   %s = appliance_name (HTML-escaped)
//   %s = appliance_model (HTML-escaped)
//   %s = appliance_serial (HTML-escaped)
//   %s = appliance_firmware (HTML-escaped)
//   %s = modbus_channel (HTML-escaped)
//   %s = modbus_ip (HTML-escaped)
//   %s = modbus_port (HTML-escaped)
//   %s = modbus_gateway (HTML-escaped)
//   %s = modbus_netmask (HTML-escaped)
//   %s = modbus_unit_id (HTML-escaped)
//   %s/%s/%s = " selected" for num_phases options 1/2/3 (exactly one non-empty)
// language=HTML
const char CAPTIVE_HTML_APPLIANCE_PARAMETERS[] = R"HTML(<fieldset>
    <legend>Appliance parameters</legend>
    <label>Name
        <input type="text" name="appliance_name" value="%s" maxlength="64">
    </label>
    <label>Model
        <input type="text" name="appliance_model" value="%s" maxlength="64">
    </label>
    <label>Serial
        <input type="text" name="appliance_serial" value="%s" maxlength="64">
    </label>
    <label>Firmware
        <input type="text" name="appliance_firmware" value="%s" maxlength="32">
    </label>
    <label>Modbus channel
        <input type="text" name="modbus_channel" value="%s" maxlength="32">
    </label>
    <label>Modbus ip
        <input type="text" name="modbus_ip" value="%s" maxlength="16">
    </label>
    <label>Modbus port
        <input type="text" name="modbus_port" value="%s" maxlength="6">
    </label>
    <label>Modbus gateway
        <input type="text" name="modbus_gateway" value="%s" maxlength="16">
    </label>
    <label>Modbus netmask
        <input type="text" name="modbus_netmask" value="%s" maxlength="16">
    </label>
    <label>Modbus unit ID
        <input type="text" name="modbus_unit_id" value="%s" maxlength="8">
    </label>
    <label>Number of phases
        <select name="num_phases">
            <option value="1"%s>1</option>
            <option value="2"%s>2</option>
            <option value="3"%s>3</option>
        </select>
    </label>
</fieldset>
)HTML";



// language=HTML
const char CAPTIVE_HTML_APPLIANCE_TAIL[] = R"HTML(<button type="submit">Save &amp; reboot</button>
</form>
</body>
</html>
)HTML";



// Saved page has no '%' in its body, so it's safe to drive a single snprintf.
// Placeholder: %u = bytes just saved.
// language=HTML
const char CAPTIVE_HTML_APPLIANCE_SAVED[] = R"HTML(<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>Saved</title>
    <style>
        body {
            font-family: system-ui, sans-serif;
            max-width: 480px;
            margin: 1em auto;
            padding: 0 1em;
            color: #222;
        }
        h1 {
            font-size: 1.2em;
        }
        .nav {
            margin: 1em 0;
            font-size: .9em;
        }
        .nav a {
            color: #0a7;
            text-decoration: none;
            display: block;
            margin: .3em 0;
        }
    </style>
</head>
<body>
<h1>Saved</h1>
<p>Stored %u bytes of appliance config, rebooting now...</p>
</body>
</html>
)HTML";
