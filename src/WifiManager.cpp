#include "WifiManager.h"

// Global objects
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// State variables
bool shouldSaveConfig = false;
String captiveSSID = "";
String captivePassword = "";

// HTML Pages with modern UI
const char HTML_HEAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>TapTrack WiFi Setup</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            padding: 40px;
            max-width: 400px;
            width: 100%;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            animation: slideUp 0.5s ease;
        }
        @keyframes slideUp {
            from {
                opacity: 0;
                transform: translateY(30px);
            }
            to {
                opacity: 1;
                transform: translateY(0);
            }
        }
        .logo {
            text-align: center;
            margin-bottom: 30px;
        }
        .logo h1 {
            color: #667eea;
            font-size: 28px;
            font-weight: 700;
        }
        .logo p {
            color: #666;
            font-size: 14px;
            margin-top: 5px;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            color: #333;
            font-weight: 600;
            margin-bottom: 8px;
            font-size: 14px;
        }
        input, select {
            width: 100%;
            padding: 12px 15px;
            border: 2px solid #e0e0e0;
            border-radius: 10px;
            font-size: 16px;
            transition: all 0.3s;
            outline: none;
        }
        input:focus, select:focus {
            border-color: #667eea;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
        }
        button {
            width: 100%;
            padding: 14px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 10px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 10px 20px rgba(102, 126, 234, 0.3);
        }
        button:active {
            transform: translateY(0);
        }
        .network-item {
            padding: 12px 15px;
            border: 2px solid #e0e0e0;
            border-radius: 10px;
            margin-bottom: 10px;
            cursor: pointer;
            transition: all 0.3s;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .network-item:hover {
            border-color: #667eea;
            background: #f8f9ff;
        }
        .network-name {
            font-weight: 600;
            color: #333;
        }
        .signal-strength {
            color: #667eea;
            font-size: 12px;
        }
        .info-box {
            background: #f0f4ff;
            padding: 15px;
            border-radius: 10px;
            margin-bottom: 20px;
            border-left: 4px solid #667eea;
        }
        .info-box p {
            color: #555;
            font-size: 14px;
            line-height: 1.5;
        }
        .status {
            text-align: center;
            padding: 10px;
            border-radius: 8px;
            margin-top: 15px;
            font-size: 14px;
        }
        .status.success {
            background: #d4edda;
            color: #155724;
        }
        .status.error {
            background: #f8d7da;
            color: #721c24;
        }
        .loading {
            display: inline-block;
            width: 20px;
            height: 20px;
            border: 3px solid rgba(255,255,255,.3);
            border-radius: 50%;
            border-top-color: #fff;
            animation: spin 1s ease-in-out infinite;
        }
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
    </style>
</head>
)rawliteral";

const char HTML_SCAN[] PROGMEM = R"rawliteral(
<body>
    <div class="container">
        <div class="logo">
            <h1>📡 TapTrack</h1>
            <p>Attendance System Setup</p>
        </div>
        <div class="info-box">
            <p>Select your WiFi network from the list below or enter manually.</p>
        </div>
        <div id="networks"></div>
        <form action="/connect" method="POST" style="margin-top: 20px;">
            <div class="form-group">
                <label for="ssid">WiFi Network</label>
                <input type="text" id="ssid" name="ssid" placeholder="Enter SSID" required>
            </div>
            <div class="form-group">
                <label for="password">Password</label>
                <input type="password" id="password" name="password" placeholder="Enter password">
            </div>
            <button type="submit">Connect to WiFi</button>
        </form>
    </div>
    <script>
        // Scan for networks
        fetch('/scan')
            .then(response => response.json())
            .then(data => {
                const networksDiv = document.getElementById('networks');
                if (data.networks && data.networks.length > 0) {
                    data.networks.forEach(network => {
                        const item = document.createElement('div');
                        item.className = 'network-item';
                        item.innerHTML = `
                            <span class="network-name">${network.ssid}</span>
                            <span class="signal-strength">${network.rssi} dBm</span>
                        `;
                        item.onclick = () => {
                            document.getElementById('ssid').value = network.ssid;
                        };
                        networksDiv.appendChild(item);
                    });
                }
            });
    </script>
</body>
</html>
)rawliteral";

const char HTML_CONNECTING[] PROGMEM = R"rawliteral(
<body>
    <div class="container">
        <div class="logo">
            <h1>📡 TapTrack</h1>
            <p>Connecting to WiFi...</p>
        </div>
        <div style="text-align: center; padding: 40px 0;">
            <div class="loading"></div>
            <p style="margin-top: 20px; color: #666;">Please wait while we connect to your network.</p>
        </div>
    </div>
    <script>
        setTimeout(() => {
            window.location.href = '/status';
        }, 5000);
    </script>
</body>
</html>
)rawliteral";

const char HTML_SUCCESS[] PROGMEM = R"rawliteral(
<body>
    <div class="container">
        <div class="logo">
            <h1>✅ Connected!</h1>
            <p>WiFi setup successful</p>
        </div>
        <div class="status success">
            <p>Your TapTrack device is now connected to WiFi.<br>The device will restart shortly.</p>
        </div>
    </div>
    <script>
        setTimeout(() => {
            document.querySelector('.status').innerHTML = '<p>Restarting device...</p>';
        }, 3000);
    </script>
</body>
</html>
)rawliteral";

const char HTML_FAILED[] PROGMEM = R"rawliteral(
<body>
    <div class="container">
        <div class="logo">
            <h1>❌ Connection Failed</h1>
            <p>Unable to connect to WiFi</p>
        </div>
        <div class="status error">
            <p>Please check your WiFi credentials and try again.</p>
        </div>
        <a href="/" style="display: block; text-align: center; margin-top: 20px; color: #667eea; text-decoration: none; font-weight: 600;">← Try Again</a>
    </div>
</body>
</html>
)rawliteral";

// Handler functions
void handleRoot() {
  String page = FPSTR(HTML_HEAD);
  page += FPSTR(HTML_SCAN);
  server.send(200, "text/html", page);
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "{\"networks\":[";
  
  for (int i = 0; i < n && i < 10; ++i) {  // Limit to 10 networks
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  
  json += "]}";
  server.send(200, "application/json", json);
}

void handleConnect() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    captiveSSID = server.arg("ssid");
    captivePassword = server.arg("password");
    
    String page = FPSTR(HTML_HEAD);
    page += FPSTR(HTML_CONNECTING);
    server.send(200, "text/html", page);
    
    shouldSaveConfig = true;
  } else {
    server.send(400, "text/plain", "Missing credentials");
  }
}

void handleStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    String page = FPSTR(HTML_HEAD);
    page += FPSTR(HTML_SUCCESS);
    server.send(200, "text/html", page);
    
    delay(2000);
    ESP.restart();
  } else {
    String page = FPSTR(HTML_HEAD);
    page += FPSTR(HTML_FAILED);
    server.send(200, "text/html", page);
  }
}

// WiFi credential management
void saveWiFiCredentials(String ssid, String password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  Serial.println(F("✅ WiFi credentials saved"));
}

bool loadWiFiCredentials(String &ssid, String &password) {
  preferences.begin("wifi", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();
  
  return (ssid.length() > 0);
}

void clearWiFiCredentials() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();
  Serial.println(F("WiFi credentials cleared"));
}

bool connectToWiFi(String ssid, String password) {
  Serial.print(F("Connecting to: "));
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print(F("✅ Connected! IP: "));
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println();
    Serial.println(F("❌ Connection failed"));
    return false;
  }
}

void startCaptivePortal() {
  Serial.println(F("Starting Captive Portal..."));
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  Serial.print(F("AP IP address: "));
  Serial.println(WiFi.softAPIP());
  
  // Start DNS server for captive portal
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/status", handleStatus);
  
  // Catch all for captive portal
  server.onNotFound(handleRoot);
  
  server.begin();
  Serial.println(F("✅ Captive portal started"));
  Serial.print(F("Connect to WiFi: "));
  Serial.println(AP_SSID);
  
  unsigned long startTime = millis();
  
  while (!shouldSaveConfig) {
    dnsServer.processNextRequest();
    server.handleClient();
    
    // Timeout check
    if (millis() - startTime > PORTAL_TIMEOUT) {
      Serial.println(F("⏱️ Portal timeout"));
      return;
    }
    
    delay(10);
  }
  
  // Try to connect with new credentials
  if (shouldSaveConfig) {
    server.handleClient();  // Handle any pending requests
    delay(1000);
    
    if (connectToWiFi(captiveSSID, captivePassword)) {
      saveWiFiCredentials(captiveSSID, captivePassword);
    }
  }
}

bool initWiFiManager() {
  String ssid, password;
  
  // Try to load saved credentials
  if (loadWiFiCredentials(ssid, password)) {
    Serial.println(F("Found saved WiFi credentials"));
    
    if (connectToWiFi(ssid, password)) {
      return true;
    } else {
      Serial.println(F("Saved credentials failed, starting portal..."));
      clearWiFiCredentials();
    }
  } else {
    Serial.println(F("No saved credentials, starting portal..."));
  }
  
  // Start captive portal if no saved credentials or connection failed
  startCaptivePortal();
  
  return (WiFi.status() == WL_CONNECTED);
}