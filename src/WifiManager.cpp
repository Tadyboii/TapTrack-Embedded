/*
 * TapTrack - WiFi Manager Implementation
 * Modern captive portal UI with offline mode support
 */

#include "WifiManager.h"
#include "indicator.h"

// =============================================================================
// GLOBAL OBJECTS
// =============================================================================

static WebServer server(80);
static DNSServer dnsServer;
static Preferences preferences;

// =============================================================================
// STATE VARIABLES
// =============================================================================

static bool portalActive = false;
static bool shouldConnect = false;
static bool startOffline = false;
static String pendingSSID = "";
static String pendingPassword = "";
static unsigned long portalStartTime = 0;
// =============================================================================
// HTML TEMPLATES - REFINED MODERN UI
// =============================================================================

const char HTML_HEAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>TapTrack Setup</title>
    <style>
        * { 
            margin: 0; 
            padding: 0; 
            box-sizing: border-box; 
        }
        
        :root {
            --primary: #6366f1;
            --primary-hover: #5558e3;
            --primary-light: rgba(99, 102, 241, 0.1);
            --success: #10b981;
            --error: #ef4444;
            --warning: #f59e0b;
            --bg-primary: #0a0f1e;
            --bg-secondary: #131a2e;
            --bg-tertiary: #1a2238;
            --text-primary: #ffffff;
            --text-secondary: #94a3b8;
            --text-muted: #64748b;
            --border: #1e293b;
            --border-hover: #334155;
            --shadow-sm: 0 1px 3px rgba(0, 0, 0, 0.3);
            --shadow-md: 0 4px 6px rgba(0, 0, 0, 0.4);
            --shadow-lg: 0 10px 25px rgba(0, 0, 0, 0.5);
        }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Inter', 'Segoe UI', Roboto, sans-serif;
            background: var(--bg-primary);
            color: var(--text-primary);
            min-height: 100vh;
            padding: 20px;
            line-height: 1.6;
        }
        
        .container {
            max-width: 440px;
            margin: 0 auto;
        }
        
        .header {
            text-align: center;
            padding: 40px 0 32px;
        }
        
        .logo {
            width: 56px;
            height: 56px;
            margin: 0 auto 16px;
            background: linear-gradient(135deg, var(--primary), #8b5cf6);
            border-radius: 16px;
            display: flex;
            align-items: center;
            justify-content: center;
            position: relative;
            box-shadow: var(--shadow-lg);
        }
        
        .logo::before {
            content: '';
            position: absolute;
            width: 24px;
            height: 24px;
            background: white;
            border-radius: 50%;
            box-shadow: 0 0 0 4px rgba(255,255,255,0.2);
        }
        
        .title {
            font-size: 32px;
            font-weight: 700;
            letter-spacing: -0.5px;
            margin-bottom: 8px;
        }
        
        .subtitle {
            color: var(--text-secondary);
            font-size: 15px;
        }
        
        .card {
            background: var(--bg-secondary);
            border-radius: 20px;
            padding: 24px;
            margin-bottom: 16px;
            border: 1px solid var(--border);
            box-shadow: var(--shadow-md);
        }
        
        .card-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 20px;
        }
        
        .card-title {
            font-size: 16px;
            font-weight: 600;
            color: var(--text-primary);
        }
        
        .btn-link {
            background: none;
            border: none;
            color: var(--primary);
            cursor: pointer;
            font-size: 14px;
            font-weight: 500;
            padding: 8px 12px;
            border-radius: 8px;
            transition: all 0.2s;
        }
        
        .btn-link:hover {
            background: var(--primary-light);
        }
        
        .btn-link:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        
        .network-list {
            display: flex;
            flex-direction: column;
            gap: 8px;
            max-height: 280px;
            overflow-y: auto;
        }
        
        .network-list::-webkit-scrollbar {
            width: 6px;
        }
        
        .network-list::-webkit-scrollbar-track {
            background: var(--bg-tertiary);
            border-radius: 3px;
        }
        
        .network-list::-webkit-scrollbar-thumb {
            background: var(--border-hover);
            border-radius: 3px;
        }
        
        .network-item {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 16px;
            background: var(--bg-tertiary);
            border-radius: 12px;
            cursor: pointer;
            transition: all 0.2s ease;
            border: 2px solid transparent;
        }
        
        .network-item:hover {
            border-color: var(--border-hover);
            transform: translateY(-1px);
        }
        
        .network-item.selected {
            border-color: var(--primary);
            background: var(--primary-light);
        }
        
        .network-info {
            display: flex;
            align-items: center;
            gap: 12px;
            flex: 1;
        }
        
        .network-icon {
            width: 20px;
            height: 20px;
            display: flex;
            align-items: center;
            justify-content: center;
            color: var(--text-muted);
        }
        
        .network-name {
            font-weight: 500;
            font-size: 15px;
        }
        
        .signal-strength {
            display: flex;
            align-items: flex-end;
            gap: 3px;
            height: 18px;
        }
        
        .signal-bar {
            width: 4px;
            background: var(--text-muted);
            border-radius: 2px;
            transition: all 0.3s;
        }
        
        .signal-bar.active { 
            background: var(--success); 
        }
        
        .signal-bar:nth-child(1) { height: 6px; }
        .signal-bar:nth-child(2) { height: 10px; }
        .signal-bar:nth-child(3) { height: 14px; }
        .signal-bar:nth-child(4) { height: 18px; }
        
        .form-group {
            margin-bottom: 20px;
        }
        
        .form-group:last-child {
            margin-bottom: 0;
        }
        
        label {
            display: block;
            font-size: 13px;
            font-weight: 600;
            margin-bottom: 10px;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        
        .input-wrapper {
            position: relative;
        }
        
        input[type="text"],
        input[type="password"] {
            width: 100%;
            padding: 14px 16px;
            background: var(--bg-tertiary);
            border: 2px solid var(--border);
            border-radius: 12px;
            color: var(--text-primary);
            font-size: 15px;
            outline: none;
            transition: all 0.2s;
        }
        
        input:focus {
            border-color: var(--primary);
            background: var(--bg-primary);
        }
        
        input::placeholder {
            color: var(--text-muted);
        }
        
        .input-icon {
            position: absolute;
            right: 14px;
            top: 50%;
            transform: translateY(-50%);
            background: none;
            border: none;
            color: var(--text-muted);
            cursor: pointer;
            font-size: 20px;
            padding: 4px;
            transition: color 0.2s;
        }
        
        .input-icon:hover {
            color: var(--text-secondary);
        }
        
        .btn {
            width: 100%;
            padding: 16px;
            border: none;
            border-radius: 12px;
            font-size: 15px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
            position: relative;
            overflow: hidden;
        }
        
        .btn::before {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: linear-gradient(135deg, rgba(255,255,255,0.1), transparent);
            opacity: 0;
            transition: opacity 0.2s;
        }
        
        .btn:hover::before {
            opacity: 1;
        }
        
        .btn-primary {
            background: linear-gradient(135deg, var(--primary), #8b5cf6);
            color: white;
            box-shadow: var(--shadow-md);
        }
        
        .btn-primary:hover {
            transform: translateY(-2px);
            box-shadow: var(--shadow-lg);
        }
        
        .btn-secondary {
            background: var(--bg-tertiary);
            color: var(--text-primary);
            border: 2px solid var(--border);
        }
        
        .btn-secondary:hover {
            border-color: var(--border-hover);
            background: var(--bg-secondary);
        }
        
        .btn:disabled {
            opacity: 0.5;
            cursor: not-allowed;
            transform: none !important;
        }
        
        .divider {
            display: flex;
            align-items: center;
            margin: 24px 0;
            color: var(--text-muted);
            font-size: 12px;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        
        .divider::before,
        .divider::after {
            content: '';
            flex: 1;
            height: 1px;
            background: var(--border);
        }
        
        .divider span {
            padding: 0 16px;
        }
        
        .mode-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 8px;
        }
        
        .mode-option {
            padding: 14px 12px;
            background: var(--bg-tertiary);
            border: 2px solid var(--border);
            border-radius: 10px;
            cursor: pointer;
            transition: all 0.2s;
            text-align: center;
        }
        
        .mode-option:hover {
            border-color: var(--border-hover);
        }
        
        .mode-option.active {
            border-color: var(--primary);
            background: var(--primary-light);
        }
        
        .mode-label {
            font-size: 13px;
            font-weight: 600;
            color: var(--text-secondary);
        }
        
        .mode-option.active .mode-label {
            color: var(--primary);
        }
        
        .status-screen {
            text-align: center;
            padding: 60px 20px;
        }
        
        .status-icon {
            width: 80px;
            height: 80px;
            margin: 0 auto 24px;
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 40px;
        }
        
        .status-icon.success {
            background: rgba(16, 185, 129, 0.15);
            color: var(--success);
        }
        
        .status-icon.error {
            background: rgba(239, 68, 68, 0.15);
            color: var(--error);
        }
        
        .status-icon.info {
            background: rgba(99, 102, 241, 0.15);
            color: var(--primary);
        }
        
        .status-title {
            font-size: 26px;
            font-weight: 700;
            margin-bottom: 12px;
            letter-spacing: -0.5px;
        }
        
        .status-message {
            color: var(--text-secondary);
            font-size: 15px;
            line-height: 1.7;
        }
        
        .spinner {
            width: 48px;
            height: 48px;
            border: 4px solid var(--border);
            border-top-color: var(--primary);
            border-radius: 50%;
            animation: spin 0.8s linear infinite;
            margin: 0 auto 24px;
        }
        
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
        
        .info-banner {
            background: var(--bg-secondary);
            border: 1px solid var(--border);
            border-left: 4px solid var(--primary);
            border-radius: 12px;
            padding: 16px 18px;
            margin-bottom: 20px;
            font-size: 14px;
            color: var(--text-secondary);
            line-height: 1.6;
        }
        
        .empty-state {
            text-align: center;
            padding: 32px 20px;
            color: var(--text-muted);
            font-size: 14px;
        }
        
        .hidden { 
            display: none !important; 
        }
        
        @media (max-width: 480px) {
            .container {
                padding: 0 8px;
            }
            
            .card {
                padding: 20px;
            }
            
            .mode-grid {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
)rawliteral";

const char HTML_MAIN[] PROGMEM = R"rawliteral(
<div class="container">
    <div class="header">
        <div class="logo"></div>
        <h1 class="title">TapTrack</h1>
        <p class="subtitle">Attendance System Setup</p>
    </div>
    
    <div class="info-banner">
        Connect to WiFi for cloud sync, or start in offline mode to store attendance locally.
    </div>
    
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">Available Networks</h2>
            <button class="btn-link" id="refresh-btn" onclick="scanNetworks()">Refresh</button>
        </div>
        <div id="network-list" class="network-list">
            <div class="empty-state">Scanning for networks...</div>
        </div>
    </div>
    
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">Connection Details</h2>
        </div>
        <form id="wifi-form" onsubmit="return connectWiFi(event)">
            <div class="form-group">
                <label for="ssid">Network Name</label>
                <input type="text" id="ssid" name="ssid" placeholder="Select or enter network name" required>
            </div>
            
            <div class="form-group">
                <label for="password">Password</label>
                <div class="input-wrapper">
                    <input type="password" id="password" name="password" placeholder="Enter network password">
                    <button type="button" class="input-icon" onclick="togglePassword()">
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                            <path id="eye-icon" d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path>
                            <circle cx="12" cy="12" r="3"></circle>
                        </svg>
                    </button>
                </div>
            </div>
            
            <div class="form-group">
                <label>Default Mode</label>
                <div class="mode-grid">
                    <div class="mode-option active" data-mode="auto" onclick="selectMode(this)">
                        <div class="mode-label">Auto</div>
                    </div>
                    <div class="mode-option" data-mode="online" onclick="selectMode(this)">
                        <div class="mode-label">Online</div>
                    </div>
                    <div class="mode-option" data-mode="offline" onclick="selectMode(this)">
                        <div class="mode-label">Offline</div>
                    </div>
                </div>
            </div>
            
            <input type="hidden" id="mode" name="mode" value="auto">
            
            <button type="submit" class="btn btn-primary" id="connect-btn">
                Connect to WiFi
            </button>
        </form>
        
        <div class="divider"><span>or</span></div>
        
        <button type="button" class="btn btn-secondary" onclick="startOfflineMode()">
            Start Offline Mode
        </button>
    </div>
</div>

<script>
let selectedNetwork = null;
let selectedMode = 'auto';
let isScanning = false;

function scanNetworks() {
    if (isScanning) return;
    
    isScanning = true;
    const list = document.getElementById('network-list');
    const refreshBtn = document.getElementById('refresh-btn');
    
    list.innerHTML = '<div class="empty-state">Scanning...</div>';
    refreshBtn.disabled = true;
    
    fetch('/scan')
        .then(r => r.json())
        .then(data => {
            if (data.networks && data.networks.length > 0) {
                list.innerHTML = data.networks.map(n => `
                    <div class="network-item" onclick="selectNetwork('${n.ssid}', this)">
                        <div class="network-info">
                            <div class="network-icon">
                                ${n.secure ? '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="5" y="11" width="14" height="10" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>' : '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="5" y="11" width="14" height="10" rx="2"/></svg>'}
                            </div>
                            <span class="network-name">${n.ssid}</span>
                        </div>
                        <div class="signal-strength">
                            <div class="signal-bar ${n.bars >= 1 ? 'active' : ''}"></div>
                            <div class="signal-bar ${n.bars >= 2 ? 'active' : ''}"></div>
                            <div class="signal-bar ${n.bars >= 3 ? 'active' : ''}"></div>
                            <div class="signal-bar ${n.bars >= 4 ? 'active' : ''}"></div>
                        </div>
                    </div>
                `).join('');
            } else {
                list.innerHTML = '<div class="empty-state">No networks found</div>';
            }
        })
        .catch(() => {
            list.innerHTML = '<div class="empty-state" style="color: var(--error);">Scan failed. Please try again.</div>';
        })
        .finally(() => {
            isScanning = false;
            refreshBtn.disabled = false;
        });
}

function selectNetwork(ssid, el) {
    document.querySelectorAll('.network-item').forEach(e => e.classList.remove('selected'));
    el.classList.add('selected');
    document.getElementById('ssid').value = ssid;
    selectedNetwork = ssid;
}

function togglePassword() {
    const input = document.getElementById('password');
    const icon = document.getElementById('eye-icon');
    
    if (input.type === 'password') {
        input.type = 'text';
        icon.setAttribute('d', 'M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24');
    } else {
        input.type = 'password';
        icon.setAttribute('d', 'M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z');
    }
}

function selectMode(el) {
    document.querySelectorAll('.mode-option').forEach(o => o.classList.remove('active'));
    el.classList.add('active');
    selectedMode = el.dataset.mode;
    document.getElementById('mode').value = selectedMode;
}

function connectWiFi(e) {
    e.preventDefault();
    const btn = document.getElementById('connect-btn');
    const originalText = btn.innerHTML;
    
    btn.disabled = true;
    btn.innerHTML = '<div class="spinner" style="width:20px;height:20px;border-width:2px;margin:0;"></div>Connecting...';
    
    const formData = new FormData(document.getElementById('wifi-form'));
    
    fetch('/connect', {
        method: 'POST',
        body: new URLSearchParams(formData)
    })
    .then(() => {
        window.location.href = '/status';
    })
    .catch(() => {
        btn.disabled = false;
        btn.innerHTML = originalText;
        alert('Connection failed. Please check your credentials and try again.');
    });
    
    return false;
}

function startOfflineMode() {
    if (confirm('Start in offline mode?\n\nAttendance will be stored locally until you connect to WiFi.')) {
        window.location.href = '/offline';
    }
}

scanNetworks();
</script>
</body>
</html>
)rawliteral";

const char HTML_CONNECTING[] PROGMEM = R"rawliteral(
<div class="container">
    <div class="card">
        <div class="status-screen">
            <div class="spinner"></div>
            <h2 class="status-title">Connecting...</h2>
            <p class="status-message">Please wait while we connect to your WiFi network.</p>
        </div>
    </div>
</div>
<script>
setTimeout(() => { window.location.href = '/status'; }, 8000);
</script>
</body>
</html>
)rawliteral";

const char HTML_SUCCESS[] PROGMEM = R"rawliteral(
<div class="container">
    <div class="card">
        <div class="status-screen">
            <div class="status-icon success">✓</div>
            <h2 class="status-title">Connected!</h2>
            <p class="status-message">WiFi setup complete.<br>The device will restart in a moment.</p>
        </div>
    </div>
</div>
<script>
setTimeout(() => {
    document.querySelector('.status-message').innerHTML = 'Restarting device...';
}, 2000);
</script>
</body>
</html>
)rawliteral";

const char HTML_FAILED[] PROGMEM = R"rawliteral(
<div class="container">
    <div class="card">
        <div class="status-screen">
            <div class="status-icon error">×</div>
            <h2 class="status-title">Connection Failed</h2>
            <p class="status-message">Could not connect to the WiFi network.<br>Please check your credentials and try again.</p>
            <button class="btn btn-primary" onclick="window.location.href='/'" style="margin-top: 32px; max-width: 200px;">
                Try Again
            </button>
        </div>
    </div>
</div>
</body>
</html>
)rawliteral";

const char HTML_OFFLINE[] PROGMEM = R"rawliteral(
<div class="container">
    <div class="card">
        <div class="status-screen">
            <div class="status-icon info">○</div>
            <h2 class="status-title">Offline Mode</h2>
            <p class="status-message">Starting in offline mode.<br>Attendance will be stored locally.</p>
        </div>
    </div>
</div>
<script>
setTimeout(() => {
    document.querySelector('.status-message').innerHTML = 'Restarting device...';
}, 2000);
</script>
</body>
</html>
)rawliteral";

// =============================================================================
// ROUTE HANDLERS
// =============================================================================

static void handleRoot() {
    String page = FPSTR(HTML_HEAD);
    page += FPSTR(HTML_MAIN);
    server.send(200, "text/html", page);
}

static void handleScan() {
    int n = WiFi.scanNetworks();
    String json = "{\"networks\":[";
    
    for (int i = 0; i < n && i < 15; ++i) {
        if (i > 0) json += ",";
        
        int rssi = WiFi.RSSI(i);
        int bars = 1;
        if (rssi > -50) bars = 4;
        else if (rssi > -60) bars = 3;
        else if (rssi > -70) bars = 2;
        
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(rssi) + ",";
        json += "\"bars\":" + String(bars) + ",";
        json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    
    json += "]}";
    server.send(200, "application/json", json);
}

static void handleConnect() {
    if (server.hasArg("ssid")) {
        pendingSSID = server.arg("ssid");
        pendingPassword = server.hasArg("password") ? server.arg("password") : "";
        
        // Parse mode
        String modeStr = server.hasArg("mode") ? server.arg("mode") : "auto";
        SystemMode mode = MODE_AUTO;
        if (modeStr == "online") mode = MODE_FORCE_ONLINE;
        else if (modeStr == "offline") mode = MODE_FORCE_OFFLINE;
        saveSystemMode(mode);
        
        String page = FPSTR(HTML_HEAD);
        page += FPSTR(HTML_CONNECTING);
        server.send(200, "text/html", page);
        
        shouldConnect = true;
    } else {
        server.send(400, "text/plain", "Missing SSID");
    }
}

static void handleStatus() {
    String page = FPSTR(HTML_HEAD);
    
    if (WiFi.status() == WL_CONNECTED) {
        page += FPSTR(HTML_SUCCESS);
        server.send(200, "text/html", page);
        delay(2000);
        ESP.restart();
    } else {
        page += FPSTR(HTML_FAILED);
        server.send(200, "text/html", page);
    }
}

static void handleOffline() {
    startOffline = true;
    saveSystemMode(MODE_FORCE_OFFLINE);
    
    String page = FPSTR(HTML_HEAD);
    page += FPSTR(HTML_OFFLINE);
    server.send(200, "text/html", page);
    
    delay(2000);
    ESP.restart();
}

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

bool initWiFiManager() {
    String ssid, password;
    
    // Try saved credentials first
    if (loadWiFiCredentials(ssid, password)) {
        Serial.println(F("📶 Found saved WiFi credentials"));
        
        if (connectToWiFi(ssid, password)) {
            return true;
        }
        
        Serial.println(F("⚠️ Saved credentials failed"));
    }
    
    // Check if we should start offline (from previous selection)
    SystemMode savedMode = loadSystemMode();
    if (savedMode == MODE_FORCE_OFFLINE) {
        Serial.println(F("📴 Starting in forced offline mode"));
        return false;
    }
    
    // Start captive portal
    Serial.println(F("🌐 Starting captive portal..."));
    startCaptivePortal();
    
    // Handle portal until connected or timeout
    portalStartTime = millis();
    while (portalActive) {
        handlePortal();
        
        if (shouldConnect) {
            shouldConnect = false;
            if (connectToWiFi(pendingSSID, pendingPassword)) {
                saveWiFiCredentials(pendingSSID, pendingPassword);
                stopCaptivePortal();
                return true;
            }
        }
        
        if (startOffline) {
            stopCaptivePortal();
            return false;
        }
        
        if (millis() - portalStartTime > PORTAL_TIMEOUT_MS) {
            Serial.println(F("⏱️ Portal timeout"));
            stopCaptivePortal();
            return false;
        }
        
        delay(10);
    }
    
    return WiFi.status() == WL_CONNECTED;
}

void startCaptivePortal() {
    indicatePortalActive(true);
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    Serial.print(F("📡 AP started: "));
    Serial.println(AP_SSID);
    Serial.print(F("   IP: "));
    Serial.println(WiFi.softAPIP());
    
    dnsServer.start(53, "*", WiFi.softAPIP());
    
    server.on("/", handleRoot);
    server.on("/scan", handleScan);
    server.on("/connect", HTTP_POST, handleConnect);
    server.on("/status", handleStatus);
    server.on("/offline", handleOffline);
    server.onNotFound(handleRoot);
    
    server.begin();
    portalActive = true;
    
    Serial.println(F("✓ Captive portal ready"));
}

void handlePortal() {
    if (portalActive) {
        dnsServer.processNextRequest();
        server.handleClient();
    }
}

bool isPortalActive() {
    return portalActive;
}

void stopCaptivePortal() {
    if (portalActive) {
        server.stop();
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        portalActive = false;
        indicatePortalActive(false);
        Serial.println(F("🛑 Captive portal stopped"));
    }
}

bool connectToWiFi(String ssid, String password, uint32_t timeout) {
    Serial.print(F("📶 Connecting to: "));
    Serial.println(ssid);
    
    indicateConnecting(true);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < timeout)) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    
    indicateConnecting(false);
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("✅ Connected! IP: "));
        Serial.println(WiFi.localIP());
        return true;
    }
    
    Serial.println(F("❌ Connection failed"));
    return false;
}

void saveWiFiCredentials(String ssid, String password) {
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();
    Serial.println(F("💾 WiFi credentials saved"));
}

bool loadWiFiCredentials(String &ssid, String &password) {
    preferences.begin("wifi", true);
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    preferences.end();
    return ssid.length() > 0;
}

void clearWiFiCredentials() {
    preferences.begin("wifi", false);
    preferences.remove("ssid");
    preferences.remove("password");
    preferences.end();
    Serial.println(F("🗑️ WiFi credentials cleared"));
}

void saveSystemMode(SystemMode mode) {
    preferences.begin("system", false);
    preferences.putInt("mode", (int)mode);
    preferences.end();
}

SystemMode loadSystemMode() {
    preferences.begin("system", true);
    int mode = preferences.getInt("mode", DEFAULT_SYSTEM_MODE);
    preferences.end();
    return (SystemMode)mode;
}

int getWiFiSignalPercent() {
    if (WiFi.status() != WL_CONNECTED) return 0;
    int rssi = WiFi.RSSI();
    if (rssi >= -50) return 100;
    if (rssi <= -100) return 0;
    return 2 * (rssi + 100);
}

int getWiFiSignalBars() {
    int percent = getWiFiSignalPercent();
    if (percent >= 75) return 4;
    if (percent >= 50) return 3;
    if (percent >= 25) return 2;
    return 1;
}

bool isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void disconnectWiFi() {
    WiFi.disconnect(true);
    Serial.println(F("📴 WiFi disconnected"));
}

bool reconnectWiFi() {
    String ssid, password;
    if (loadWiFiCredentials(ssid, password)) {
        return connectToWiFi(ssid, password, 10000);
    }
    return false;
}
