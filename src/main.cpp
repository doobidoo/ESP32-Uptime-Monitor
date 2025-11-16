#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <stdarg.h>
#include <LittleFS.h>
#include <ESPmDNS.h>

// Include AsyncWebServer after WiFiManager to avoid conflicts
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>

// --- Configuration Version ---
const int CONFIG_VERSION = 13;  // Incremented for breaking changes
const int NUM_TARGETS = 20;  // Increased from 3 to 20
const int EEPROM_SIZE = 4095;
const int CONFIG_VERSION_ADDRESS = 4090;

// --- Data Structure for a single target ---
struct TargetConfig {
    char server_name[32];
    char group_name[32];  // NEW: Group/tab name for organization
    char weburl[128];
    char discord_webhook_url[128];
    char ntfy_url[64];
    char ntfy_priority[16];
    char telegram_bot_token[50];
    char telegram_chat_id_1[16];
    char telegram_chat_id_2[16];
    char telegram_chat_id_3[16];
    char http_get_url_on[128];
    char http_get_url_off[128];
    char online_message[128];
    char offline_message[128];
    uint16_t check_interval_seconds;
    uint8_t failure_threshold;
    uint8_t recovery_threshold;
    bool enabled;  // NEW: Whether this server is active
};

// --- Global variables for operation ---
int gmt_offset = 1; // Default GMT offset
TargetConfig targets[NUM_TARGETS];

// --- WiFiManager flag ---
bool shouldSaveConfig = false;

// --- Runtime state variables ---
long gmtOffset_sec;
const char* ntpServer = "pool.ntp.org";
int httpCode[NUM_TARGETS] = {0};
unsigned long pingTime[NUM_TARGETS] = {0};
unsigned long minpingTime[NUM_TARGETS] = {0};
unsigned long maxpingTime[NUM_TARGETS] = {0};
unsigned long last_check_time[NUM_TARGETS] = {0};
uint8_t failure_count[NUM_TARGETS] = {0};
uint8_t success_count[NUM_TARGETS] = {0};
bool confirmed_online_state[NUM_TARGETS] = {true};

// --- Buffers for logs to prevent memory fragmentation ---
const int TARGET_LOG_SIZE = 1024;
const int SERIAL_LOG_SIZE = 2048;
char targetLogMessages[NUM_TARGETS][TARGET_LOG_SIZE];
char serialLogBuffer[SERIAL_LOG_SIZE];
int serialLogBufferPos = 0;

// --- WiFi Reconnection Timer ---
unsigned long lastWifiReconnectAttempt = 0;
const long wifiReconnectInterval = 10000; // Try to reconnect every 10 seconds

AsyncWebServer server(80);

// --- HTML for the Firmware Update Page ---
const char* UPDATE_HTML = R"rawliteral(
<form method='POST' action='/updatefirmware' enctype='multipart/form-data' id='upload_form'><h2>Firmware Update</h2><p>Upload the new .bin file here. <strong>Warning:</strong> After the update, all settings will be reset to their default values.</p><input type='file' name='update' id='file' onchange='sub(this)' style='display:none'><label id='file-input' for='file'>Choose File...</label><input type='submit' class='btn' value='Start Update'><br><br><div id='prg'>Progress: 0%</div><br><div id='prgbar'><div id='bar'></div></div></form><script>function sub(obj){var fileName=obj.value.split('\\').pop();document.getElementById('file-input').innerHTML=fileName}
document.getElementById('upload_form').onsubmit=function(e){e.preventDefault();var form=document.getElementById('upload_form');var data=new FormData(form);var xhr=new XMLHttpRequest();xhr.open('POST','/updatefirmware',true);xhr.upload.onprogress=function(evt){if(evt.lengthComputable){var per=Math.round((evt.loaded/evt.total)*100);document.getElementById('prg').innerHTML='Progress: '+per+'%';document.getElementById('bar').style.width=per+'%'}};xhr.onload=function(){if(xhr.status===200){alert('Update successful! The device will restart with default settings.')}else{alert('Update failed! Status: '+xhr.status)}};xhr.send(data)};</script><style>body{background:#121212;font-family:sans-serif;font-size:14px;color:#e0e0e0}form{background:#1e1e1e;max-width:300px;margin:75px auto;padding:30px;border-radius:8px;text-align:center;border:1px solid #333}#file-input,.btn{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px}.btn{background:#3498db;color:#fff;cursor:pointer;border:0;padding:0 15px}.btn:hover{background-color:#2980b9}#file-input{padding:0;border:1px solid #333;line-height:44px;text-align:left;display:block;cursor:pointer;padding-left:10px}#prgbar{background-color:#333;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px;border-radius:10px}</style>
)rawliteral";


// --- HTML, CSS & JS for the modern web interface ---
const char* INDEX_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Server Status</title>
    <style>
        :root {
            --bg-color: #121212;
            --card-bg: #1e1e1e;
            --font-color: #e0e0e0;
            --color-green: #2ecc71;
            --color-red: #e74c3c;
            --color-blue: #3498db;
            --color-orange: #f39c12;
            --border-color: #333;
            --border-radius: 8px;
        }
        * { box-sizing: border-box; }
        body { background-color: var(--bg-color); color: var(--font-color); font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; padding-bottom: 80px; }
        .container { max-width: 1200px; margin: auto; }
        .main-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; flex-wrap: wrap; gap: 10px; }
        .main-header h1 { margin: 0; font-size: 1.8em; }
        .header-buttons { display: flex; gap: 10px; }
        .status-indicator { width: 12px; height: 12px; border-radius: 50%; margin-right: 8px; background-color: #7f8c8d; transition: background-color 0.3s; display: inline-block; }
        .status-indicator.online { background-color: var(--color-green); }
        .status-indicator.offline { background-color: var(--color-red); }
        .card { background: var(--card-bg); padding: 20px; border-radius: var(--border-radius); border: 1px solid var(--border-color); margin-top: 20px; }
        button, .button-link, .btn { background: var(--color-blue); color: white !important; border: none; padding: 10px 15px; border-radius: 5px; cursor: pointer; transition: background-color 0.2s; font-size: 0.95em; text-decoration: none; display: inline-block; text-align: center; }
        button:hover, .button-link:hover, .btn:hover { background-color: #2980b9; }
        .btn-small { padding: 5px 10px; font-size: 0.85em; }
        .btn-danger { background-color: var(--color-red); }
        .btn-danger:hover { background-color: #c0392b; }
        .btn-success { background-color: var(--color-green); }
        .btn-success:hover { background-color: #27ae60; }
        .modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; overflow: auto; background-color: rgba(0,0,0,0.7); }
        .modal-content { background-color: var(--card-bg); margin: 5% auto; padding: 0; border: 1px solid var(--border-color); width: 90%; max-width: 700px; border-radius: var(--border-radius); }
        .modal-header { padding: 15px 20px; border-bottom: 1px solid var(--border-color); display: flex; justify-content: space-between; align-items: center; }
        .modal-header h2 { margin: 0; }
        .close { color: #aaa; font-size: 28px; font-weight: bold; cursor: pointer; }
        .modal-body { padding: 20px; max-height: 60vh; overflow-y: auto;}
        .modal-footer { padding: 15px 20px; border-top: 1px solid var(--border-color); text-align: right; }
        .tab-buttons { border-bottom: 1px solid var(--border-color); padding: 0 10px; display: flex; gap: 5px; flex-wrap: wrap;}
        .tab-buttons button { background: none; border: none; padding: 10px 15px; cursor: pointer; color: #888; border-bottom: 3px solid transparent; font-size: 1em; }
        .tab-buttons button.active { color: var(--color-blue); border-bottom-color: var(--color-blue); }
        .tab-content { display: none; }
        .tab-content.active { display: block; }
        input, select, textarea { width: 100%; padding: 10px; margin: 5px 0; display: inline-block; border: 1px solid var(--border-color); border-radius: 4px; background-color: #333; color: var(--font-color); font-family: inherit;}
        textarea { resize: vertical; min-height: 80px;}
        form label.section-label { margin-top: 20px; display: block; font-weight: bold; font-size: 1.1em; color: var(--font-color); border-bottom: 1px solid var(--border-color); padding-bottom: 5px; margin-bottom: 15px; }
        form label { color: #ccc; }
        p.description { font-size: 0.8em; color: #888; margin-top: -2px; margin-bottom: 8px; }
        .form-group { margin-bottom: 15px; }
        .form-row { display: flex; gap: 10px; }
        .form-row .form-group { flex: 1; }
        hr { border-color: var(--border-color); margin: 20px 0; }
        .version-info { text-align: center; margin-top: 15px; color: #888; }

        /* Server Table Styles */
        .server-table { width: 100%; border-collapse: collapse; margin-top: 15px; }
        .server-table th { text-align: left; padding: 12px; border-bottom: 2px solid var(--border-color); color: #999; font-weight: 500; font-size: 0.9em; }
        .server-row { cursor: pointer; transition: background-color 0.2s; border-bottom: 1px solid var(--border-color); }
        .server-row:hover { background-color: rgba(255,255,255,0.05); }
        .server-row td { padding: 15px 12px; }
        .server-row.expanded { background-color: rgba(52, 152, 219, 0.1); }
        .server-details { display: none; background-color: rgba(0,0,0,0.2); }
        .server-details.show { display: table-row; }
        .server-details td { padding: 20px; }
        .details-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }
        .detail-item { background: rgba(255,255,255,0.05); padding: 10px; border-radius: 5px; }
        .detail-item strong { display: block; color: #999; font-size: 0.85em; margin-bottom: 5px; }
        .detail-actions { margin-top: 15px; display: flex; gap: 10px; }
        .timeline { max-height: 300px; overflow-y: auto; margin-top: 15px; padding-left: 20px; border-left: 2px solid var(--border-color); }
        .timeline-event { margin-bottom: 15px; position: relative; padding: 10px; background: rgba(255, 255, 255, 0.05); border-radius: 6px; }
        .timeline-event::before { content: ''; position: absolute; left: -28px; top: 15px; width: 12px; height: 12px; border-radius: 50%; background: var(--bg-color); border: 2px solid var(--border-color); }
        .status-on { border-left: 4px solid var(--color-green); }
        .status-on::before { border-color: var(--color-green); }
        .status-off { border-left: 4px solid var(--color-red); }
        .status-off::before { border-color: var(--color-red); }
        .timeline-event time { display: block; font-size: 0.8em; color: #999; margin-bottom: 5px; }
        .timeline-event p { margin: 0; }

        /* Floating Add Button */
        .fab { position: fixed; bottom: 30px; right: 30px; width: 60px; height: 60px; border-radius: 50%; background-color: var(--color-green); color: white; font-size: 28px; border: none; cursor: pointer; box-shadow: 0 4px 10px rgba(0,0,0,0.3); transition: all 0.3s; z-index: 999; display: flex; align-items: center; justify-content: center; }
        .fab:hover { background-color: #27ae60; transform: scale(1.1); }

        .empty-state { text-align: center; padding: 40px 20px; color: #999; }
        .badge { display: inline-block; padding: 3px 8px; border-radius: 3px; font-size: 0.8em; margin-left: 5px; }
        .badge-count { background-color: var(--color-blue); color: white; }
    </style>
</head>
<body>
    <div class="container">
        <header class="main-header">
            <h1>Server Status Monitor</h1>
            <div class="header-buttons">
                <button id="logBtn" style="background-color: var(--color-orange);">Logs</button>
                <button id="settingsBtn">Settings</button>
            </div>
        </header>

        <main>
            <div class="tab-buttons" id="group-tabs"></div>
            <div id="group-content"></div>
        </main>
    </div>

    <!-- Floating Add Button -->
    <button class="fab" id="addServerBtn" title="Add Server">+</button>

    <!-- General Settings Modal -->
    <div id="settingsModal" class="modal">
      <div class="modal-content">
        <div class="modal-header">
            <h2>General Settings</h2>
            <span class="close" id="closeSettingsBtn">&times;</span>
        </div>
        <div class="modal-body">
            <div class="form-group"><label for="ssid_input">WiFi SSID (Read-only)</label><input type="text" id="ssid_input" disabled></div>
            <div class="form-group"><label for="gmt_offset_input">UTC Offset (hours)</label><input type="number" id="gmt_offset_input" min="-12" max="14"></div>
            <p class="description">Note: WiFi credentials are managed via the WiFiManager portal. To change WiFi, reset the device.</p>
        </div>
        <div class="modal-footer">
            <button id="saveGeneralBtn">Save</button>
        </div>
        <div class="modal-body">
            <hr>
            <a href="/update" class="button-link" style="width: 100%; background-color: #e67e22;">Firmware Update</a>
            <p class="version-info" id="firmware_version">Current Version: ...</p>
        </div>
      </div>
    </div>

    <!-- Add/Edit Server Modal -->
    <div id="serverModal" class="modal">
      <div class="modal-content">
        <div class="modal-header">
            <h2 id="serverModalTitle">Add Server</h2>
            <span class="close" id="closeServerBtn">&times;</span>
        </div>
        <form id="serverForm">
            <div class="modal-body">
                <input type="hidden" id="server_id">
                <div class="form-row">
                    <div class="form-group"><label for="server_name">Server Name</label><input type="text" id="server_name" required></div>
                    <div class="form-group"><label for="server_group">Group</label><input type="text" id="server_group" placeholder="Production" required></div>
                </div>
                <div class="form-group"><label for="server_url">URL to Monitor</label><input type="url" id="server_url" placeholder="https://example.com" required></div>

                <label class="section-label">Monitoring Settings</label>
                <div class="form-row">
                    <div class="form-group"><label for="check_interval">Check Interval (seconds)</label><input type="number" id="check_interval" value="60" min="5"></div>
                    <div class="form-group"><label for="failure_threshold">Failures for Alert</label><input type="number" id="failure_threshold" value="3" min="1"></div>
                    <div class="form-group"><label for="recovery_threshold">Successes for Recovery</label><input type="number" id="recovery_threshold" value="2" min="1"></div>
                </div>

                <label class="section-label">Notification Messages</label>
                <div class="form-group"><label for="online_message">Online Message</label><p class="description">Placeholders: {NAME}, {URL}</p><textarea id="online_message">{NAME} is back online!</textarea></div>
                <div class="form-group"><label for="offline_message">Offline Message</label><p class="description">Placeholders: {NAME}, {URL}, {CODE}</p><textarea id="offline_message">{NAME} is down!</textarea></div>

                <label class="section-label">Notification Channels</label>
                <div class="form-group"><label for="discord_webhook">Discord Webhook URL</label><p class="description">'0' to disable</p><input type="text" id="discord_webhook" value="0"></div>
                <div class="form-group"><label for="ntfy_url">Ntfy Topic URL</label><p class="description">'0' to disable</p><input type="text" id="ntfy_url" value="0"></div>
                <div class="form-group"><label for="ntfy_priority">Ntfy Priority</label><select id="ntfy_priority"><option value="default">Default</option><option value="min">Minimal</option><option value="high">High</option><option value="max">Maximum</option></select></div>
                <div class="form-group"><label for="telegram_bot_token">Telegram Bot Token</label><p class="description">'0' to disable</p><input type="text" id="telegram_bot_token" value="0"></div>
                <div class="form-row">
                    <div class="form-group"><label for="telegram_chat_id_1">Chat ID 1</label><input type="text" id="telegram_chat_id_1" value="0"></div>
                    <div class="form-group"><label for="telegram_chat_id_2">Chat ID 2</label><input type="text" id="telegram_chat_id_2" value="0"></div>
                    <div class="form-group"><label for="telegram_chat_id_3">Chat ID 3</label><input type="text" id="telegram_chat_id_3" value="0"></div>
                </div>

                <label class="section-label">Custom HTTP Actions</label>
                <div class="form-group"><label for="http_get_url_on">On 'Server Online'</label><p class="description">'0' to disable</p><input type="text" id="http_get_url_on" value="0"></div>
                <div class="form-group"><label for="http_get_url_off">On 'Server Offline'</label><p class="description">'0' to disable</p><input type="text" id="http_get_url_off" value="0"></div>
            </div>
            <div class="modal-footer">
                <button type="submit" class="btn btn-success">Save Server</button>
            </div>
        </form>
      </div>
    </div>

    <!-- Log Modal -->
    <div id="logModal" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <h2>Serial Log</h2>
                <span class="close" id="closeLogBtn">&times;</span>
            </div>
            <div class="modal-body">
                <pre id="log-content" style="white-space: pre-wrap; word-wrap: break-word; background-color: #111; padding: 10px; border-radius: 5px; color: #e0e0e0; font-family: monospace; max-height: 60vh; overflow-y: auto;"></pre>
            </div>
        </div>
    </div>

    <script>
        document.addEventListener('DOMContentLoaded', () => {
            let refreshIntervalId, logRefreshIntervalId;
            let allServersData = [];
            let currentGroup = 'All';

            // Modal elements
            const settingsModal = document.getElementById('settingsModal');
            const settingsBtn = document.getElementById('settingsBtn');
            const closeSettingsBtn = document.getElementById('closeSettingsBtn');
            const saveGeneralBtn = document.getElementById('saveGeneralBtn');

            const serverModal = document.getElementById('serverModal');
            const addServerBtn = document.getElementById('addServerBtn');
            const closeServerBtn = document.getElementById('closeServerBtn');
            const serverForm = document.getElementById('serverForm');
            const serverModalTitle = document.getElementById('serverModalTitle');

            const logModal = document.getElementById('logModal');
            const logBtn = document.getElementById('logBtn');
            const closeLogBtn = document.getElementById('closeLogBtn');
            const logContent = document.getElementById('log-content');

            // Auto-refresh functions
            function startAutoRefresh() { clearInterval(refreshIntervalId); fetchData(); refreshIntervalId = setInterval(fetchData, 5000); }
            function stopAutoRefresh() { clearInterval(refreshIntervalId); }

            // Modal handlers
            settingsBtn.onclick = () => { stopAutoRefresh(); settingsModal.style.display = 'block'; }
            closeSettingsBtn.onclick = () => { settingsModal.style.display = 'none'; startAutoRefresh(); }

            addServerBtn.onclick = () => {
                stopAutoRefresh();
                serverModalTitle.textContent = 'Add Server';
                serverForm.reset();
                document.getElementById('server_id').value = '';
                document.getElementById('server_group').value = currentGroup === 'All' ? 'Production' : currentGroup;
                serverModal.style.display = 'block';
            };
            closeServerBtn.onclick = () => { serverModal.style.display = 'none'; startAutoRefresh(); }

            logBtn.onclick = () => {
                stopAutoRefresh();
                logModal.style.display = 'block';
                fetchLogs();
                logRefreshIntervalId = setInterval(fetchLogs, 2000);
            };
            closeLogBtn.onclick = () => {
                logModal.style.display = 'none';
                clearInterval(logRefreshIntervalId);
                startAutoRefresh();
            };

            window.onclick = (event) => {
                if (event.target == settingsModal) { settingsModal.style.display = 'none'; startAutoRefresh(); }
                if (event.target == serverModal) { serverModal.style.display = 'none'; startAutoRefresh(); }
                if (event.target == logModal) { closeLogBtn.onclick(); }
            };

            // Save general settings
            saveGeneralBtn.onclick = async () => {
                const gmtOffset = document.getElementById('gmt_offset_input').value;
                const params = new URLSearchParams({gmt_offset: gmtOffset});
                try {
                    const res = await fetch('/api/settings', {method: 'POST', body: params});
                    if (res.ok) {
                        alert('Settings saved! Device will restart.');
                        settingsModal.style.display = 'none';
                    } else alert('Error saving settings.');
                } catch (error) {
                    console.error('Error:', error);
                    alert('Error saving settings.');
                }
            };

            // Server form submission (Add/Edit)
            serverForm.onsubmit = async (e) => {
                e.preventDefault();
                const serverId = document.getElementById('server_id').value;
                const isEdit = serverId !== '';
                const endpoint = isEdit ? '/api/server/update' : '/api/server/add';

                const data = {
                    name: document.getElementById('server_name').value,
                    group: document.getElementById('server_group').value,
                    url: document.getElementById('server_url').value,
                    check_interval: parseInt(document.getElementById('check_interval').value),
                    failure_threshold: parseInt(document.getElementById('failure_threshold').value),
                    recovery_threshold: parseInt(document.getElementById('recovery_threshold').value),
                    online_message: document.getElementById('online_message').value,
                    offline_message: document.getElementById('offline_message').value,
                    discord_webhook: document.getElementById('discord_webhook').value,
                    ntfy_url: document.getElementById('ntfy_url').value,
                    ntfy_priority: document.getElementById('ntfy_priority').value,
                    telegram_bot_token: document.getElementById('telegram_bot_token').value,
                    telegram_chat_id_1: document.getElementById('telegram_chat_id_1').value,
                    telegram_chat_id_2: document.getElementById('telegram_chat_id_2').value,
                    telegram_chat_id_3: document.getElementById('telegram_chat_id_3').value,
                    http_get_url_on: document.getElementById('http_get_url_on').value,
                    http_get_url_off: document.getElementById('http_get_url_off').value
                };

                if (isEdit) data.id = parseInt(serverId);

                try {
                    const res = await fetch(endpoint, {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify(data)
                    });
                    const result = await res.json();
                    if (result.success) {
                        alert(isEdit ? 'Server updated!' : 'Server added!');
                        serverModal.style.display = 'none';
                        fetchData();
                    } else {
                        alert('Error: ' + (result.error || 'Unknown error'));
                    }
                } catch (error) {
                    console.error('Error:', error);
                    alert('Error saving server.');
                }
            };

            // Fetch logs
            async function fetchLogs() {
                try {
                    const response = await fetch('/api/logs');
                    if (!response.ok) throw new Error('Log fetch failed');
                    logContent.textContent = await response.text();
                    logContent.scrollTop = logContent.scrollHeight;
                } catch (error) {
                    console.error('Error fetching logs:', error);
                    logContent.textContent = 'Error loading logs.';
                }
            }

            // Fetch server data
            async function fetchData() {
                try {
                    const response = await fetch('/api/status');
                    if (!response.ok) throw new Error('Network response was not ok');
                    const data = await response.json();
                    allServersData = data.targets;
                    document.getElementById('ssid_input').value = data.general_config.ssid;
                    document.getElementById('gmt_offset_input').value = data.general_config.gmt_offset;
                    document.getElementById('firmware_version').textContent = 'Current Version: ' + (data.firmware_version / 10).toFixed(1);
                    updateUI();
                } catch (error) { console.error('Error fetching status data:', error); }
            }

            // Update UI with group tabs and server table
            function updateUI() {
                const groupTabs = document.getElementById('group-tabs');
                const groupContent = document.getElementById('group-content');

                // Extract unique groups
                const groups = new Set(['All']);
                allServersData.forEach(s => {
                    if (s.config.enabled) groups.add(s.config.group_name);
                });

                // Render group tabs
                groupTabs.innerHTML = '';
                Array.from(groups).forEach((group, idx) => {
                    const btn = document.createElement('button');
                    btn.className = `tab-link ${group === currentGroup ? 'active' : ''}`;
                    btn.textContent = group;
                    const count = group === 'All' ? allServersData.filter(s => s.config.enabled).length : allServersData.filter(s => s.config.enabled && s.config.group_name === group).length;
                    if (count > 0) btn.innerHTML += `<span class="badge badge-count">${count}</span>`;
                    btn.onclick = () => {
                        currentGroup = group;
                        updateUI();
                    };
                    groupTabs.appendChild(btn);
                });

                // Filter servers by current group
                const filteredServers = allServersData.filter(s => {
                    if (!s.config.enabled) return false;
                    return currentGroup === 'All' || s.config.group_name === currentGroup;
                });

                // Render server table
                if (filteredServers.length === 0) {
                    groupContent.innerHTML = '<div class="card"><div class="empty-state"><h3>No servers in this group</h3><p>Click the + button to add a server</p></div></div>';
                } else {
                    groupContent.innerHTML = `
                        <div class="card">
                            <table class="server-table">
                                <thead>
                                    <tr>
                                        <th>Name</th>
                                        <th>Status</th>
                                        <th>URL</th>
                                        <th>Ping (ms)</th>
                                        <th>Actions</th>
                                    </tr>
                                </thead>
                                <tbody id="serverTableBody"></tbody>
                            </table>
                        </div>
                    `;

                    const tbody = document.getElementById('serverTableBody');
                    filteredServers.forEach(server => renderServerRow(tbody, server));
                }
            }

            // Render individual server row with expandable details
            function renderServerRow(tbody, server) {
                const isOnline = server.http_code >= 200 && server.http_code < 400;
                const rowId = `server-row-${server.id}`;

                // Main row
                const row = document.createElement('tr');
                row.className = 'server-row';
                row.id = rowId;
                row.innerHTML = `
                    <td><strong>${server.config.server_name}</strong></td>
                    <td><span class="status-indicator ${isOnline ? 'online' : 'offline'}"></span> ${isOnline ? 'Online' : `Offline (${server.http_code})`}</td>
                    <td style="word-break: break-all; max-width: 300px;">${server.config.weburl}</td>
                    <td>${server.ping.last} ms</td>
                    <td>
                        <button class="btn btn-small" onclick="event.stopPropagation(); editServer(${server.id})">Edit</button>
                        <button class="btn btn-small btn-danger" onclick="event.stopPropagation(); deleteServer(${server.id}, '${server.config.server_name}')">Delete</button>
                    </td>
                `;
                row.onclick = () => toggleServerDetails(server.id);
                tbody.appendChild(row);

                // Details row (hidden by default)
                const detailsRow = document.createElement('tr');
                detailsRow.className = 'server-details';
                detailsRow.id = `server-details-${server.id}`;
                detailsRow.innerHTML = `
                    <td colspan="5">
                        <div class="details-grid">
                            <div class="detail-item"><strong>Group</strong>${server.config.group_name}</div>
                            <div class="detail-item"><strong>Check Interval</strong>${server.config.check_interval_seconds}s</div>
                            <div class="detail-item"><strong>Min Ping</strong>${server.ping.min} ms</div>
                            <div class="detail-item"><strong>Max Ping</strong>${server.ping.max} ms</div>
                            <div class="detail-item"><strong>Failure Threshold</strong>${server.config.failure_threshold}</div>
                            <div class="detail-item"><strong>Recovery Threshold</strong>${server.config.recovery_threshold}</div>
                        </div>
                        <h4 style="margin-top: 20px;">Uptime Log</h4>
                        <div class="timeline" id="timeline-${server.id}"></div>
                    </td>
                `;
                tbody.appendChild(detailsRow);

                // Populate timeline
                const timeline = detailsRow.querySelector(`#timeline-${server.id}`);
                const logEntries = server.log.split('\\n').filter(e => e.trim() !== '');
                if (logEntries.length === 0) {
                    timeline.innerHTML = '<p>No log entries yet.</p>';
                } else {
                    logEntries.forEach(entry => {
                        const parts = entry.split(';');
                        if (parts.length < 2) return;
                        const status = parts[0];
                        const time = parts[1];
                        const isEntryOnline = (status === 'on');
                        const text = isEntryOnline ? 'Server Online' : 'Server Offline';
                        const eventDiv = document.createElement('div');
                        eventDiv.className = `timeline-event ${isEntryOnline ? 'status-on' : 'status-off'}`;
                        eventDiv.innerHTML = `<time>${time}</time><p>${text}</p>`;
                        timeline.appendChild(eventDiv);
                    });
                }
            }

            // Toggle server details expansion
            function toggleServerDetails(serverId) {
                const row = document.getElementById(`server-row-${serverId}`);
                const details = document.getElementById(`server-details-${serverId}`);
                row.classList.toggle('expanded');
                details.classList.toggle('show');
            }

            // Edit server
            window.editServer = (id) => {
                const server = allServersData.find(s => s.id === id);
                if (!server) return;

                stopAutoRefresh();
                serverModalTitle.textContent = 'Edit Server';
                document.getElementById('server_id').value = id;
                document.getElementById('server_name').value = server.config.server_name;
                document.getElementById('server_group').value = server.config.group_name;
                document.getElementById('server_url').value = server.config.weburl;
                document.getElementById('check_interval').value = server.config.check_interval_seconds;
                document.getElementById('failure_threshold').value = server.config.failure_threshold;
                document.getElementById('recovery_threshold').value = server.config.recovery_threshold;
                document.getElementById('online_message').value = server.config.online_message;
                document.getElementById('offline_message').value = server.config.offline_message;
                document.getElementById('discord_webhook').value = server.config.discord_webhook;
                document.getElementById('ntfy_url').value = server.config.ntfy_url;
                document.getElementById('ntfy_priority').value = server.config.ntfy_priority;
                document.getElementById('telegram_bot_token').value = server.config.telegram_bot_token;
                document.getElementById('telegram_chat_id_1').value = server.config.telegram_chat_id_1;
                document.getElementById('telegram_chat_id_2').value = server.config.telegram_chat_id_2;
                document.getElementById('telegram_chat_id_3').value = server.config.telegram_chat_id_3;
                document.getElementById('http_get_url_on').value = server.config.http_get_url_on;
                document.getElementById('http_get_url_off').value = server.config.http_get_url_off;
                serverModal.style.display = 'block';
            };

            // Delete server
            window.deleteServer = async (id, name) => {
                if (!confirm(`Delete server "${name}"?`)) return;
                try {
                    const res = await fetch('/api/server/delete', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({id})
                    });
                    const result = await res.json();
                    if (result.success) {
                        alert('Server deleted!');
                        fetchData();
                    } else {
                        alert('Error: ' + (result.error || 'Unknown error'));
                    }
                } catch (error) {
                    console.error('Error:', error);
                    alert('Error deleting server.');
                }
            };

            startAutoRefresh();
        });
    </script>
</body>
</html>
)rawliteral";


// --- HELPER FUNCTIONS ---

void getFormattedTime(char* buffer, size_t bufferSize) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        strncpy(buffer, "Time not set", bufferSize -1);
        buffer[bufferSize -1] = '\0';
        return;
    }
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

void web_log_printf(const char *format, ...) {
    char timeBuf[30];
    getFormattedTime(timeBuf, sizeof(timeBuf));

    char logBuf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(logBuf, sizeof(logBuf), format, args);
    va_end(args);

    Serial.printf("[%s] %s\n", timeBuf, logBuf);

    int len = snprintf(NULL, 0, "[%s] %s\n", timeBuf, logBuf);
    if (serialLogBufferPos + len >= SERIAL_LOG_SIZE) {
        serialLogBufferPos = 0;
    }
    snprintf(serialLogBuffer + serialLogBufferPos, SERIAL_LOG_SIZE - serialLogBufferPos, "[%s] %s\n", timeBuf, logBuf);
    serialLogBufferPos += len;
}

void prependToLog(char* logBuffer, const char* newEntry, size_t bufferSize) {
    size_t entryLen = strlen(newEntry);
    if (entryLen >= bufferSize) return;

    size_t currentLen = strlen(logBuffer);
    
    if (currentLen + entryLen >= bufferSize) {
        currentLen = bufferSize - entryLen - 1;
    }

    memmove(logBuffer + entryLen, logBuffer, currentLen);
    memcpy(logBuffer, newEntry, entryLen);
    logBuffer[currentLen + entryLen] = '\0';
}

void safeStrcpy(char* dest, const char* src, size_t size) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

void urlEncode(char* dst, const char* src, size_t dstSize) {
    char c, hex_buf[4];
    size_t written = 0;
    while (*src && written + 4 < dstSize) {
        c = *src++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[written++] = c;
        } else {
            sprintf(hex_buf, "%%%02X", c);
            dst[written++] = hex_buf[0];
            dst[written++] = hex_buf[1];
            dst[written++] = hex_buf[2];
        }
    }
    dst[written] = '\0';
}

void resetToDefault() {
    Serial.println("Resetting to default values...");

    // Clear EEPROM
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
    EEPROM.put(CONFIG_VERSION_ADDRESS, CONFIG_VERSION);
    EEPROM.commit();
    EEPROM.end();
    Serial.println("EEPROM cleared.");

    // Clear LittleFS config
    if (LittleFS.exists("/config.json")) {
        LittleFS.remove("/config.json");
        Serial.println("LittleFS config cleared.");
    }

    // NOTE: WiFi credentials are NOT cleared here
    // WiFiManager stores credentials in NVS independently of app config
    // WiFi credentials should persist across app config version changes
    // Use factoryReset() function instead if you need to clear WiFi credentials

    Serial.println("App settings reset to defaults (WiFi credentials preserved).");
}

void factoryReset() {
    Serial.println("Performing FACTORY RESET - clearing ALL settings including WiFi...");

    // Clear app settings
    resetToDefault();

    // Clear WiFiManager settings (WiFi credentials)
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    Serial.println("WiFi credentials cleared.");

    Serial.println("Factory reset complete. Device will restart.");
}

// WiFiManager callback notifying us of the need to save config
void saveConfigCallback() {
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

// Forward declaration
void saveConfig();

void loadConfig() {
    bool configLoaded = false;

    // Load configuration from LittleFS
    if (LittleFS.exists("/config.json")) {
        File configFile = LittleFS.open("/config.json", "r");
        if (configFile) {
            Serial.println("Reading config file");
            size_t size = configFile.size();
            std::unique_ptr<char[]> buf(new char[size]);
            configFile.readBytes(buf.get(), size);

            // Larger buffer for 20 servers (10KB)
            DynamicJsonDocument json(10240);

            if (deserializeJson(json, buf.get()) == DeserializationError::Ok) {
                Serial.println("Successfully parsed config");
                gmt_offset = json["gmt_offset"] | 1;
                Serial.printf("Loaded GMT offset: %d\n", gmt_offset);

                // Load server configurations
                JsonArray servers = json["servers"];
                if (servers) {
                    int loadedCount = 0;
                    for (JsonObject server : servers) {
                        int i = server["id"] | loadedCount;
                        if (i >= 0 && i < NUM_TARGETS) {
                            strlcpy(targets[i].server_name, server["name"] | "", sizeof(targets[i].server_name));
                            strlcpy(targets[i].group_name, server["group"] | "Default", sizeof(targets[i].group_name));
                            strlcpy(targets[i].weburl, server["url"] | "0", sizeof(targets[i].weburl));
                            targets[i].enabled = server["enabled"] | false;
                            targets[i].check_interval_seconds = server["check_interval"] | 20;
                            targets[i].failure_threshold = server["failure_threshold"] | 3;
                            targets[i].recovery_threshold = server["recovery_threshold"] | 2;

                            strlcpy(targets[i].discord_webhook_url, server["discord_webhook"] | "0", sizeof(targets[i].discord_webhook_url));
                            strlcpy(targets[i].ntfy_url, server["ntfy_url"] | "0", sizeof(targets[i].ntfy_url));
                            strlcpy(targets[i].ntfy_priority, server["ntfy_priority"] | "default", sizeof(targets[i].ntfy_priority));
                            strlcpy(targets[i].telegram_bot_token, server["telegram_token"] | "0", sizeof(targets[i].telegram_bot_token));
                            strlcpy(targets[i].telegram_chat_id_1, server["telegram_chat1"] | "0", sizeof(targets[i].telegram_chat_id_1));
                            strlcpy(targets[i].telegram_chat_id_2, server["telegram_chat2"] | "0", sizeof(targets[i].telegram_chat_id_2));
                            strlcpy(targets[i].telegram_chat_id_3, server["telegram_chat3"] | "0", sizeof(targets[i].telegram_chat_id_3));
                            strlcpy(targets[i].http_get_url_on, server["http_url_on"] | "0", sizeof(targets[i].http_get_url_on));
                            strlcpy(targets[i].http_get_url_off, server["http_url_off"] | "0", sizeof(targets[i].http_get_url_off));
                            strlcpy(targets[i].online_message, server["msg_online"] | "✅ {NAME} is back online: {URL}", sizeof(targets[i].online_message));
                            strlcpy(targets[i].offline_message, server["msg_offline"] | "🚨 {NAME} OUTAGE: {URL} (Code: {CODE})", sizeof(targets[i].offline_message));

                            loadedCount++;
                        }
                    }
                    Serial.printf("Loaded %d servers from config\n", loadedCount);
                    configLoaded = true;
                }
            } else {
                Serial.println("Failed to parse JSON config");
            }
            configFile.close();
        }
    }

    // Initialize with defaults if no config exists
    if (!configLoaded) {
        Serial.println("Initializing default configuration");
        for (int i = 0; i < NUM_TARGETS; i++) {
            char name[32];
            snprintf(name, sizeof(name), "Server %d", i + 1);
            safeStrcpy(targets[i].server_name, name, sizeof(targets[i].server_name));
            strcpy(targets[i].group_name, (i < 3) ? "Production" : "Staging");
            strcpy(targets[i].weburl, (i == 0) ? "http://localhost" : "0");
            targets[i].enabled = (i < 3);  // Only first 3 enabled by default

            strcpy(targets[i].discord_webhook_url, "0");
            strcpy(targets[i].ntfy_url, "0");
            strcpy(targets[i].ntfy_priority, "default");
            strcpy(targets[i].telegram_bot_token, "0");
            strcpy(targets[i].telegram_chat_id_1, "0");
            strcpy(targets[i].telegram_chat_id_2, "0");
            strcpy(targets[i].telegram_chat_id_3, "0");
            strcpy(targets[i].http_get_url_on, "0");
            strcpy(targets[i].http_get_url_off, "0");
            safeStrcpy(targets[i].online_message, "✅ {NAME} is back online: {URL}", sizeof(targets[i].online_message));
            safeStrcpy(targets[i].offline_message, "🚨 {NAME} OUTAGE: {URL} (Code: {CODE})", sizeof(targets[i].offline_message));
            targets[i].check_interval_seconds = 20;
            targets[i].failure_threshold = 3;
            targets[i].recovery_threshold = 2;
        }
        // Save the default config
        saveConfig();
    }

    gmtOffset_sec = gmt_offset * 3600;
}

void saveConfig() {
    Serial.println("Saving config to LittleFS");

    // Larger buffer for 20 servers (10KB)
    DynamicJsonDocument json(10240);

    json["gmt_offset"] = gmt_offset;
    json["config_version"] = CONFIG_VERSION;

    // Create servers array
    JsonArray servers = json.createNestedArray("servers");

    for (int i = 0; i < NUM_TARGETS; i++) {
        // Only save enabled servers or those with configuration
        if (targets[i].enabled || strlen(targets[i].weburl) > 1) {
            JsonObject server = servers.createNestedObject();

            server["id"] = i;
            server["name"] = targets[i].server_name;
            server["group"] = targets[i].group_name;
            server["url"] = targets[i].weburl;
            server["enabled"] = targets[i].enabled;
            server["check_interval"] = targets[i].check_interval_seconds;
            server["failure_threshold"] = targets[i].failure_threshold;
            server["recovery_threshold"] = targets[i].recovery_threshold;

            server["discord_webhook"] = targets[i].discord_webhook_url;
            server["ntfy_url"] = targets[i].ntfy_url;
            server["ntfy_priority"] = targets[i].ntfy_priority;
            server["telegram_token"] = targets[i].telegram_bot_token;
            server["telegram_chat1"] = targets[i].telegram_chat_id_1;
            server["telegram_chat2"] = targets[i].telegram_chat_id_2;
            server["telegram_chat3"] = targets[i].telegram_chat_id_3;
            server["http_url_on"] = targets[i].http_get_url_on;
            server["http_url_off"] = targets[i].http_get_url_off;
            server["msg_online"] = targets[i].online_message;
            server["msg_offline"] = targets[i].offline_message;
        }
    }

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("Failed to open config file for writing");
        return;
    }

    size_t bytesWritten = serializeJson(json, configFile);
    configFile.close();

    if (bytesWritten == 0) {
        Serial.println("Failed to write JSON to file");
    } else {
        Serial.printf("Config saved (%d bytes)\n", bytesWritten);
    }

    // Update version in EEPROM for compatibility checking
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(CONFIG_VERSION_ADDRESS, CONFIG_VERSION);
    EEPROM.commit();
    EEPROM.end();
}

void updatePingStats(int index) {
    if (pingTime[index] < minpingTime[index] || minpingTime[index] == 0) minpingTime[index] = pingTime[index];
    if (pingTime[index] > maxpingTime[index]) maxpingTime[index] = pingTime[index];
}

void sendNotifications(int index, const char* msg) {
    TargetConfig target = targets[index];
    HTTPClient http;

    if (strcmp(target.discord_webhook_url, "0") != 0 && strlen(target.discord_webhook_url) > 10) {
        http.begin(target.discord_webhook_url);
        http.addHeader("Content-Type", "application/json");
        char payload[256];
        snprintf(payload, sizeof(payload), "{\"content\":\"%s\"}", msg);
        http.POST(payload);
        http.end();
    }
    if (strcmp(target.ntfy_url, "0") != 0 && strlen(target.ntfy_url) > 10) {
        http.begin(target.ntfy_url);
        http.addHeader("Content-Type", "text/plain");
        http.addHeader("Priority", target.ntfy_priority);
        http.POST(msg);
        http.end();
    }
    if (strlen(target.telegram_bot_token) > 10) {
        char encodedMsg[512];
        urlEncode(encodedMsg, msg, sizeof(encodedMsg));
        
        char url[512];
        const char* chat_ids[] = {target.telegram_chat_id_1, target.telegram_chat_id_2, target.telegram_chat_id_3};
        for (int j = 0; j < 3; j++) {
            if (strcmp(chat_ids[j], "0") != 0 && strlen(chat_ids[j]) > 1) {
                snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage?chat_id=%s&text=%s", target.telegram_bot_token, chat_ids[j], encodedMsg);
                http.begin(url);
                http.GET();
                http.end();
            }
        }
    }
}

void sendCustomHttpRequest(const char* url) {
    if (strcmp(url, "0") == 0 || strlen(url) < 10) return;
    HTTPClient http;
    http.begin(url);
    http.GET();
    http.end();
}

void manageWifiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiReconnectAttempt > wifiReconnectInterval) {
      lastWifiReconnectAttempt = millis();
      web_log_printf("WiFi connection lost. Attempting to reconnect...");
      WiFi.reconnect(); // Reconnect using stored credentials
    }
  }
}

void setup() {
    Serial.begin(115200);
    web_log_printf("Booting device...");

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        web_log_printf("Failed to mount LittleFS");
    } else {
        web_log_printf("LittleFS mounted successfully");
    }

    EEPROM.begin(EEPROM_SIZE);
    int storedVersion = 0;
    EEPROM.get(CONFIG_VERSION_ADDRESS, storedVersion);
    EEPROM.end();
    if (storedVersion != CONFIG_VERSION) {
        web_log_printf("Config version mismatch! Stored: %d, Firmware: %d. Resetting.", storedVersion, CONFIG_VERSION);
        resetToDefault();
        delay(1000);
        ESP.restart();
    }

    // TEMPORARILY DISABLED - GPIO0 strapping pin issue
    // Will use web interface or different GPIO for reset
    bool forceConfigPortal = false;

    // TODO: Move button functionality to a different GPIO pin
    // GPIO0 is a strapping pin and causes boot issues

    // Load configuration from LittleFS and EEPROM
    loadConfig();

    // WiFiManager setup
    WiFiManager wifiManager;

    // Enable debug output
    wifiManager.setDebugOutput(true);

    // Set callback for saving config
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    // Custom parameter for GMT offset
    char gmtOffsetStr[4];
    sprintf(gmtOffsetStr, "%d", gmt_offset);
    WiFiManagerParameter custom_gmt_offset("gmt_offset", "UTC Offset (hours)", gmtOffsetStr, 4);
    wifiManager.addParameter(&custom_gmt_offset);

    // Configure WiFiManager for better iOS compatibility
    wifiManager.setConfigPortalBlocking(true);
    wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
        Serial.println("==========================================");
        Serial.println("CONFIG PORTAL STARTED!");
        Serial.println("==========================================");
        Serial.println("AP SSID: " + String(myWiFiManager->getConfigPortalSSID()));
        Serial.println("AP IP: " + WiFi.softAPIP().toString());
        Serial.println("Connect to the AP and open: http://192.168.4.1");
        Serial.println("==========================================");
    });

    // Set a minimum quality of signal to attempt connection
    wifiManager.setMinimumSignalQuality(10);

    // Try to connect automatically, if fails start portal
    wifiManager.setConnectRetries(3);

    // Set connection timeout to 10 seconds (reduces wait time when portal disappears)
    wifiManager.setConnectTimeout(10);

    // Disable config portal timeout (keep portal open indefinitely)
    wifiManager.setConfigPortalTimeout(0);

    web_log_printf("Starting WiFi configuration...");

    bool wifiConnected = false;

    if (forceConfigPortal) {
        // Force start config portal
        web_log_printf("Starting config portal (forced by button press)...");
        wifiConnected = wifiManager.startConfigPortal("ESP32-Uptime-Monitor");
    } else {
        // Auto connect (tries saved credentials first, then starts portal)
        web_log_printf("Attempting auto-connect...");
        wifiConnected = wifiManager.autoConnect("ESP32-Uptime-Monitor");
    }

    if (!wifiConnected) {
        web_log_printf("Failed to connect - restarting...");
        delay(3000);
        ESP.restart();
    }

    // WiFi connected
    web_log_printf("==========================================");
    web_log_printf("WiFi connected successfully!");
    web_log_printf("==========================================");
    web_log_printf("SSID: %s", WiFi.SSID().c_str());
    web_log_printf("IP Address: %s", WiFi.localIP().toString().c_str());
    web_log_printf("==========================================");

    // Save GMT offset if it was changed
    int newGmtOffset = atoi(custom_gmt_offset.getValue());
    if (newGmtOffset != gmt_offset) {
        gmt_offset = newGmtOffset;
        shouldSaveConfig = true;
    }

    if (shouldSaveConfig) {
        saveConfig();
        shouldSaveConfig = false;
    }

    gmtOffset_sec = gmt_offset * 3600;
    configTime(gmtOffset_sec, 0, ntpServer);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", INDEX_HTML);
    });

    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", serialLogBuffer);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncJsonResponse * response = new AsyncJsonResponse(false, 16384);  // 16KB buffer for 20 servers
        JsonObject root = response->getRoot();

        root["firmware_version"] = CONFIG_VERSION;
        JsonObject general_config = root.createNestedObject("general_config");
        general_config["ssid"] = WiFi.SSID();
        general_config["gmt_offset"] = gmt_offset;

        JsonArray targets_json = root.createNestedArray("targets");
        for (int i = 0; i < NUM_TARGETS; i++) {
            JsonObject target_obj = targets_json.createNestedObject();
            target_obj["id"] = i;
            target_obj["http_code"] = httpCode[i];
            target_obj["log"] = targetLogMessages[i];
            JsonObject ping = target_obj.createNestedObject("ping");
            ping["last"] = pingTime[i];
            ping["min"] = minpingTime[i];
            ping["max"] = maxpingTime[i];

            JsonObject config = target_obj.createNestedObject("config");
            config["server_name"] = targets[i].server_name;
            config["group_name"] = targets[i].group_name;
            config["enabled"] = targets[i].enabled;
            config["weburl"] = targets[i].weburl;
            config["discord_webhook"] = targets[i].discord_webhook_url;
            config["ntfy_url"] = targets[i].ntfy_url;
            config["ntfy_priority"] = targets[i].ntfy_priority;
            config["telegram_bot_token"] = targets[i].telegram_bot_token;
            config["telegram_chat_id_1"] = targets[i].telegram_chat_id_1;
            config["telegram_chat_id_2"] = targets[i].telegram_chat_id_2;
            config["telegram_chat_id_3"] = targets[i].telegram_chat_id_3;
            config["http_get_url_on"] = targets[i].http_get_url_on;
            config["http_get_url_off"] = targets[i].http_get_url_off;
            config["online_message"] = targets[i].online_message;
            config["offline_message"] = targets[i].offline_message;
            config["check_interval_seconds"] = targets[i].check_interval_seconds;
            config["failure_threshold"] = targets[i].failure_threshold;
            config["recovery_threshold"] = targets[i].recovery_threshold;
        }

        response->setLength();
        request->send(response);
    });
    
    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
        for (int i = 0; i < request->params(); i++) {
            const AsyncWebParameter* p = request->getParam(i);
            if (!p->isPost()) continue;

            const char* paramName = p->name().c_str();
            const char* paramValue = p->value().c_str();

            // Note: WiFi credentials are now managed by WiFiManager
            // To change WiFi, reset the device and reconfigure through the portal
            if (strcmp(paramName, "gmt_offset") == 0) gmt_offset = atoi(paramValue);
            else {
                const char* lastUnderscore = strrchr(paramName, '_');
                if (lastUnderscore == NULL) continue;

                int serverIndex = atoi(lastUnderscore + 1);
                if (serverIndex >= 0 && serverIndex < NUM_TARGETS) {
                    char settingNameBuf[64];
                    strncpy(settingNameBuf, paramName, lastUnderscore - paramName);
                    settingNameBuf[lastUnderscore - paramName] = '\0';

                    if (strstr(settingNameBuf, "telegram_chat_id")) strcpy(settingNameBuf, "telegram_chat_id");

                    if (strcmp(settingNameBuf, "server_name") == 0) safeStrcpy(targets[serverIndex].server_name, paramValue, sizeof(targets[serverIndex].server_name));
                    else if (strcmp(settingNameBuf, "weburl") == 0) safeStrcpy(targets[serverIndex].weburl, paramValue, sizeof(targets[serverIndex].weburl));
                    else if (strcmp(settingNameBuf, "check_interval") == 0) targets[serverIndex].check_interval_seconds = atoi(paramValue);
                    else if (strcmp(settingNameBuf, "failure_threshold") == 0) targets[serverIndex].failure_threshold = atoi(paramValue);
                    else if (strcmp(settingNameBuf, "recovery_threshold") == 0) targets[serverIndex].recovery_threshold = atoi(paramValue);
                    else if (strcmp(settingNameBuf, "online_message") == 0) safeStrcpy(targets[serverIndex].online_message, paramValue, sizeof(targets[serverIndex].online_message));
                    else if (strcmp(settingNameBuf, "offline_message") == 0) safeStrcpy(targets[serverIndex].offline_message, paramValue, sizeof(targets[serverIndex].offline_message));
                    else if (strcmp(settingNameBuf, "discord_webhook") == 0) safeStrcpy(targets[serverIndex].discord_webhook_url, paramValue, sizeof(targets[serverIndex].discord_webhook_url));
                    else if (strcmp(settingNameBuf, "ntfy_url") == 0) safeStrcpy(targets[serverIndex].ntfy_url, paramValue, sizeof(targets[serverIndex].ntfy_url));
                    else if (strcmp(settingNameBuf, "ntfy_priority") == 0) safeStrcpy(targets[serverIndex].ntfy_priority, paramValue, sizeof(targets[serverIndex].ntfy_priority));
                    else if (strcmp(settingNameBuf, "telegram_bot_token") == 0) safeStrcpy(targets[serverIndex].telegram_bot_token, paramValue, sizeof(targets[serverIndex].telegram_bot_token));
                    else if (strcmp(settingNameBuf, "telegram_chat_id") == 0) {
                        char idNum = paramName[lastUnderscore - paramName - 1];
                        if (idNum == '1') safeStrcpy(targets[serverIndex].telegram_chat_id_1, paramValue, sizeof(targets[serverIndex].telegram_chat_id_1));
                        else if (idNum == '2') safeStrcpy(targets[serverIndex].telegram_chat_id_2, paramValue, sizeof(targets[serverIndex].telegram_chat_id_2));
                        else if (idNum == '3') safeStrcpy(targets[serverIndex].telegram_chat_id_3, paramValue, sizeof(targets[serverIndex].telegram_chat_id_3));
                    }
                    else if (strcmp(settingNameBuf, "http_get_url_on") == 0) safeStrcpy(targets[serverIndex].http_get_url_on, paramValue, sizeof(targets[serverIndex].http_get_url_on));
                    else if (strcmp(settingNameBuf, "http_get_url_off") == 0) safeStrcpy(targets[serverIndex].http_get_url_off, paramValue, sizeof(targets[serverIndex].http_get_url_off));
                }
            }
        }
        saveConfig();
        request->send(200, "text/plain", "OK");
        delay(1000);
        ESP.restart();
    });

    // GET /api/groups - Get list of unique groups
    server.on("/api/groups", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncJsonResponse * response = new AsyncJsonResponse();
        JsonArray groups = response->getRoot().to<JsonArray>();

        // Collect unique group names
        String uniqueGroups[NUM_TARGETS];
        int groupCount = 0;

        for (int i = 0; i < NUM_TARGETS; i++) {
            if (!targets[i].enabled) continue;

            String groupName = String(targets[i].group_name);
            bool found = false;
            for (int j = 0; j < groupCount; j++) {
                if (uniqueGroups[j] == groupName) {
                    found = true;
                    break;
                }
            }
            if (!found && groupCount < NUM_TARGETS) {
                uniqueGroups[groupCount++] = groupName;
            }
        }

        // Add to JSON array
        for (int i = 0; i < groupCount; i++) {
            groups.add(uniqueGroups[i]);
        }

        response->setLength();
        request->send(response);
    });

    // POST /api/server/add - Add new server
    server.on("/api/server/add", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                DynamicJsonDocument json(1024);
                if (deserializeJson(json, (char*)data) == DeserializationError::Ok) {
                    // Find first available slot
                    int slot = -1;
                    for (int i = 0; i < NUM_TARGETS; i++) {
                        if (!targets[i].enabled && strlen(targets[i].weburl) <= 1) {
                            slot = i;
                            break;
                        }
                    }

                    if (slot >= 0) {
                        safeStrcpy(targets[slot].server_name, json["name"] | "", sizeof(targets[slot].server_name));
                        safeStrcpy(targets[slot].group_name, json["group"] | "Default", sizeof(targets[slot].group_name));
                        safeStrcpy(targets[slot].weburl, json["url"] | "0", sizeof(targets[slot].weburl));
                        targets[slot].enabled = true;
                        targets[slot].check_interval_seconds = json["check_interval"] | 60;
                        targets[slot].failure_threshold = json["failure_threshold"] | 3;
                        targets[slot].recovery_threshold = json["recovery_threshold"] | 2;
                        safeStrcpy(targets[slot].online_message, json["online_message"] | "{NAME} is back online!", sizeof(targets[slot].online_message));
                        safeStrcpy(targets[slot].offline_message, json["offline_message"] | "{NAME} is down!", sizeof(targets[slot].offline_message));

                        saveConfig();
                        request->send(200, "application/json", "{\"success\":true,\"id\":" + String(slot) + "}");
                    } else {
                        request->send(400, "application/json", "{\"success\":false,\"error\":\"No available slots\"}");
                    }
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
                }
            }
        });

    // POST /api/server/delete - Delete (disable) server
    server.on("/api/server/delete", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                DynamicJsonDocument json(256);
                if (deserializeJson(json, (char*)data) == DeserializationError::Ok) {
                    int id = json["id"] | -1;
                    if (id >= 0 && id < NUM_TARGETS) {
                        targets[id].enabled = false;
                        strcpy(targets[id].weburl, "0");
                        saveConfig();
                        request->send(200, "application/json", "{\"success\":true}");
                    } else {
                        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid server ID\"}");
                    }
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
                }
            }
        });

    // POST /api/server/update - Update server configuration
    server.on("/api/server/update", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                DynamicJsonDocument json(2048);
                if (deserializeJson(json, (char*)data) == DeserializationError::Ok) {
                    int id = json["id"] | -1;
                    if (id >= 0 && id < NUM_TARGETS) {
                        if (json.containsKey("name")) safeStrcpy(targets[id].server_name, json["name"], sizeof(targets[id].server_name));
                        if (json.containsKey("group")) safeStrcpy(targets[id].group_name, json["group"], sizeof(targets[id].group_name));
                        if (json.containsKey("url")) safeStrcpy(targets[id].weburl, json["url"], sizeof(targets[id].weburl));
                        if (json.containsKey("enabled")) targets[id].enabled = json["enabled"];
                        if (json.containsKey("check_interval")) targets[id].check_interval_seconds = json["check_interval"];
                        if (json.containsKey("failure_threshold")) targets[id].failure_threshold = json["failure_threshold"];
                        if (json.containsKey("recovery_threshold")) targets[id].recovery_threshold = json["recovery_threshold"];
                        if (json.containsKey("online_message")) safeStrcpy(targets[id].online_message, json["online_message"], sizeof(targets[id].online_message));
                        if (json.containsKey("offline_message")) safeStrcpy(targets[id].offline_message, json["offline_message"], sizeof(targets[id].offline_message));
                        if (json.containsKey("discord_webhook")) safeStrcpy(targets[id].discord_webhook_url, json["discord_webhook"], sizeof(targets[id].discord_webhook_url));
                        if (json.containsKey("ntfy_url")) safeStrcpy(targets[id].ntfy_url, json["ntfy_url"], sizeof(targets[id].ntfy_url));
                        if (json.containsKey("ntfy_priority")) safeStrcpy(targets[id].ntfy_priority, json["ntfy_priority"], sizeof(targets[id].ntfy_priority));
                        if (json.containsKey("telegram_bot_token")) safeStrcpy(targets[id].telegram_bot_token, json["telegram_bot_token"], sizeof(targets[id].telegram_bot_token));
                        if (json.containsKey("telegram_chat_id_1")) safeStrcpy(targets[id].telegram_chat_id_1, json["telegram_chat_id_1"], sizeof(targets[id].telegram_chat_id_1));
                        if (json.containsKey("telegram_chat_id_2")) safeStrcpy(targets[id].telegram_chat_id_2, json["telegram_chat_id_2"], sizeof(targets[id].telegram_chat_id_2));
                        if (json.containsKey("telegram_chat_id_3")) safeStrcpy(targets[id].telegram_chat_id_3, json["telegram_chat_id_3"], sizeof(targets[id].telegram_chat_id_3));
                        if (json.containsKey("http_get_url_on")) safeStrcpy(targets[id].http_get_url_on, json["http_get_url_on"], sizeof(targets[id].http_get_url_on));
                        if (json.containsKey("http_get_url_off")) safeStrcpy(targets[id].http_get_url_off, json["http_get_url_off"], sizeof(targets[id].http_get_url_off));

                        saveConfig();
                        request->send(200, "application/json", "{\"success\":true}");
                    } else {
                        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid server ID\"}");
                    }
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
                }
            }
        });

    // POST /api/group/rename - Rename a group across all servers
    server.on("/api/group/rename", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                DynamicJsonDocument json(256);
                if (deserializeJson(json, (char*)data) == DeserializationError::Ok) {
                    const char* oldName = json["old_name"];
                    const char* newName = json["new_name"];
                    if (oldName && newName) {
                        int updated = 0;
                        for (int i = 0; i < NUM_TARGETS; i++) {
                            if (strcmp(targets[i].group_name, oldName) == 0) {
                                safeStrcpy(targets[i].group_name, newName, sizeof(targets[i].group_name));
                                updated++;
                            }
                        }
                        saveConfig();
                        request->send(200, "application/json", "{\"success\":true,\"updated\":" + String(updated) + "}");
                    } else {
                        request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing old_name or new_name\"}");
                    }
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
                }
            }
        });

    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", UPDATE_HTML);
    });
    
    server.on("/updatefirmware", HTTP_POST, 
        [](AsyncWebServerRequest *request) {
            request->send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        }, 
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                web_log_printf("Update Start: %s", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
            }
            if (len) {
                if (Update.write(data, len) != len) Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    web_log_printf("Update Success: %u bytes", index + len);
                    // Config migration handled by setup() on next boot
                    delay(1000);
                    ESP.restart();
                } else {
                    Update.printError(Serial);
                }
            }
        });

    // Start mDNS
    if (MDNS.begin("esp32-uptime-monitor")) {
        web_log_printf("mDNS responder started");
        web_log_printf("Access at: http://esp32-uptime-monitor.local");
        MDNS.addService("http", "tcp", 80);
    } else {
        web_log_printf("Error starting mDNS");
    }

    server.begin();
    web_log_printf("Web server started on port 80");
    web_log_printf("==========================================");
}


void loop() {
    manageWifiConnection();

    if (WiFi.status() == WL_CONNECTED) {
        for (int i = 0; i < NUM_TARGETS; i++) {
            // Skip disabled servers
            if (!targets[i].enabled) {
                httpCode[i] = 0;
                continue;
            }

            if (strcmp(targets[i].weburl, "0") == 0 || strlen(targets[i].weburl) < 10) {
                httpCode[i] = 0;
                continue;
            }

            if (millis() - last_check_time[i] < (targets[i].check_interval_seconds * 1000)) {
                continue;
            }
            last_check_time[i] = millis();

            HTTPClient http;
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            http.begin(targets[i].weburl);
            unsigned long singleStartTime = millis();
            int currentHttpCode = http.GET();
            unsigned long singleEndTime = millis();
            http.end();
            
            pingTime[i] = singleEndTime - singleStartTime;
            httpCode[i] = currentHttpCode;
            updatePingStats(i);

            bool isOnline = (httpCode[i] >= 200 && httpCode[i] < 400);

            if (isOnline) {
                failure_count[i] = 0;
                success_count[i]++;
                if (confirmed_online_state[i] == false && success_count[i] >= targets[i].recovery_threshold) {
                    confirmed_online_state[i] = true;

                    String message = targets[i].online_message;
                    message.replace("{NAME}", targets[i].server_name);
                    message.replace("{URL}", targets[i].weburl);

                    char timeBuf[30], logEntry[64];
                    getFormattedTime(timeBuf, sizeof(timeBuf));
                    snprintf(logEntry, sizeof(logEntry), "on;%s\n", timeBuf);

                    sendNotifications(i, message.c_str());
                    sendCustomHttpRequest(targets[i].http_get_url_on);
                    prependToLog(targetLogMessages[i], logEntry, TARGET_LOG_SIZE);
                }
            } else {
                success_count[i] = 0;
                failure_count[i]++;
                if (confirmed_online_state[i] == true && failure_count[i] >= targets[i].failure_threshold) {
                    confirmed_online_state[i] = false;
                    
                    String message = targets[i].offline_message;
                    message.replace("{NAME}", targets[i].server_name);
                    message.replace("{URL}", targets[i].weburl);
                    message.replace("{CODE}", String(httpCode[i]));

                    char timeBuf[30], logEntry[64];
                    getFormattedTime(timeBuf, sizeof(timeBuf));
                    snprintf(logEntry, sizeof(logEntry), "off;%s\n", timeBuf);

                    sendNotifications(i, message.c_str());
                    sendCustomHttpRequest(targets[i].http_get_url_off);
                    prependToLog(targetLogMessages[i], logEntry, TARGET_LOG_SIZE);
                }
            }
            web_log_printf("[Server %d] URL: %s, Status: %d, Ping: %lu ms, Fails: %d, Successes: %d",
                i + 1, targets[i].weburl, httpCode[i], pingTime[i], failure_count[i], success_count[i]);
        }
    }
    delay(100);
}
