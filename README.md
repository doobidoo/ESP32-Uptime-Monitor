# ESP32 Uptime Monitor v13

Turn your ESP32 into a powerful, standalone uptime monitor for your network infrastructure. Monitor up to **20 servers/websites** with a modern web interface, group organization, and comprehensive notification options.

## Features

### Monitoring Capabilities
* **Monitor up to 20 servers/websites** simultaneously
* **Group-based organization** - Organize servers into custom groups (Production, Staging, Development, etc.)
* **Real-time status updates** - Auto-refresh every 5 seconds
* **Configurable check intervals** - Set individual check frequency per server (default: 20 seconds)
* **Smart failure detection** - Configurable failure/recovery thresholds to prevent false alerts
* **Ping statistics** - Track min/max/current response times
* **Uptime timeline** - Visual history of server status changes

### Modern Web Interface
* **Clean table layout** - Server name, status, URL, ping time, and actions at a glance
* **Expandable row details** - Click any server to view full configuration and statistics
* **Tab navigation** with server count badges per group
* **Floating "Add Server" button** for easy management
* **Inline editing** - Edit or delete servers directly from the dashboard
* **Empty state guidance** - Helpful prompts when no servers are configured

### Notification Platforms
* **Discord** (via Webhook)
* **Ntfy** (with priority settings)
* **Telegram** (via Bot - supports up to 3 chat IDs per server)
* **Custom HTTP Actions** - Trigger URLs for online/offline events (IFTTT, Home Assistant, etc.)

### Advanced Features
* **RESTful API** - Full CRUD operations for server management
* **OTA Firmware Updates** - Update firmware via web interface
* **Persistent WiFi credentials** - No re-configuration needed after firmware updates
* **WiFiManager portal** - Easy WiFi setup via captive portal
* **mDNS support** - Access via `http://esp32-uptime-monitor.local`
* **LittleFS storage** - Flexible JSON-based configuration
* **Automatic config migration** - Seamless upgrades between firmware versions

## Getting Started

### Hardware Requirements
* ESP32 development board (ESP32-WROOM-32 or compatible)
* USB cable for programming
* **2.4GHz WiFi network** (ESP32 does not support 5GHz)

### Installation

#### Option 1: PlatformIO (Recommended)
```bash
# Clone the repository
git clone https://github.com/doobidoo/ESP32-Uptime-Monitor.git
cd ESP32-Uptime-Monitor

# Build and upload
pio run --target upload

# Monitor serial output
pio device monitor
```

#### Option 2: Pre-built Binary
Download the latest `ESP32_webstatusv13.bin` from the releases page and flash using:
```bash
esptool.py --port /dev/ttyUSB0 write_flash 0x10000 ESP32_webstatusv13.bin
```

### Initial Setup

1. **Power on your ESP32**
   - On first boot, the device will create a WiFi access point: `ESP32-Uptime-Monitor`

2. **Connect to the config portal**
   - Connect your phone/computer to `ESP32-Uptime-Monitor` WiFi
   - Portal should auto-open, or manually navigate to: `http://192.168.4.1`
   - If portal doesn't auto-open, **disable mobile data** on your phone and type the URL manually

3. **Configure WiFi**
   - Select your **2.4GHz WiFi network** from the list
   - Enter password
   - Click "Save"
   - Device will connect and restart

4. **Access the dashboard**
   - Find the ESP32's IP address in your router's DHCP client list
   - Or use mDNS: `http://esp32-uptime-monitor.local`
   - Open in browser to access the monitoring dashboard

### Adding Servers

1. Click the **"+ Add Server"** floating button
2. Fill in server details:
   - **Name**: Friendly name (e.g., "Pi-Hole", "Home Server")
   - **Group**: Organization category (e.g., "Production", "Staging")
   - **URL**: Full URL to monitor (e.g., `http://192.168.1.100:8080`)
   - **Check Interval**: How often to check (seconds)
   - **Thresholds**: Failure/recovery counts before triggering notifications
3. Click "Add Server"
4. Server appears immediately in the dashboard

## API Endpoints

The device provides a full RESTful API:

### Status & Information
- `GET /` - Web dashboard
- `GET /api/status` - Get all servers status (JSON)
- `GET /api/logs` - Get device logs
- `GET /api/groups` - List all server groups

### Server Management
- `POST /api/server/add` - Add new server
- `POST /api/server/update` - Update server configuration
- `POST /api/server/delete` - Delete/disable server
- `POST /api/group/rename` - Rename group across all servers

### System
- `GET /update` - Firmware update page
- `POST /updatefirmware` - Upload new firmware

## Important Notes

### WiFi Credentials Persist! ✅
**Version 13 Fix:** WiFi credentials are now stored independently in NVS (Non-Volatile Storage) and **persist across firmware updates**. You no longer need to reconfigure WiFi after OTA updates!

The device will only enter WiFiManager portal mode if:
- First boot (no credentials saved)
- WiFi credentials are invalid/incorrect
- Connection fails after multiple attempts

### 2.4GHz WiFi Only ⚠️
ESP32 hardware **does not support 5GHz WiFi bands**. The firmware automatically restricts scanning to 2.4GHz channels (1-13).

**If you have a dual-band router:**
- Ensure 2.4GHz band is enabled
- Device will only connect to the 2.4GHz SSID
- Or create a separate 2.4GHz-only SSID

### Configuration Storage
- **WiFi credentials**: Stored in ESP32 NVS (independent of app config)
- **Server settings**: Stored in LittleFS `/config.json`
- **Config version**: Stored in EEPROM for migration detection

### Factory Reset
To completely reset the device (including WiFi credentials):
1. Connect via serial terminal
2. The device currently has no button-based factory reset
3. Alternative: Flash new firmware to trigger WiFiManager portal

## Technical Specifications

- **Firmware Version**: 13
- **Platform**: ESP32 (Arduino Framework)
- **Max Servers**: 20
- **Storage**: LittleFS (JSON config)
- **WiFi**: 2.4GHz only (802.11 b/g/n)
- **Web Server**: AsyncWebServer
- **Flash Usage**: 89.6% (~1.17MB)
- **RAM Usage**: 28.5% (~93KB)

## Troubleshooting

### Web interface not accessible
- Wait 30-60 seconds after boot for WiFi connection
- Check if device is on the correct network segment
- Try accessing via mDNS: `http://esp32-uptime-monitor.local`
- Check router firewall rules

### WiFi connection fails
- Verify using **2.4GHz network** (not 5GHz)
- Check password is correct (case-sensitive)
- Try accessing config portal: Reset device and reconfigure
- Check router's DHCP pool isn't full

### Servers not appearing in dashboard
- Check if using modern browser (Chrome, Firefox, Safari)
- Clear browser cache and refresh
- Verify server was added successfully (check `/api/status`)
- Ensure server is marked as "enabled"

### Config portal doesn't open on phone
- **Disable mobile data** on your phone
- Manually type `http://192.168.4.1` in browser (don't wait for auto-popup)
- Try connecting from a computer instead
- Force WiFi scan refresh on your device

## Contributing

Issues and pull requests welcome! Please check existing issues before creating new ones.

## Credits

Based on [ESP32-Uptime-Monitor](https://github.com/PearXP/esp32-web-server-status) by PearXP.

Version 13 represents a complete architectural rewrite with:
- Converted from Arduino .ino to PlatformIO project
- Complete UI/UX redesign
- Expanded from 3 to 20 server capacity
- New RESTful API
- LittleFS storage migration
- WiFi credential persistence
- Critical stability fixes

## License

See original project for licensing information.

---

**Current Status**: v13 - Stable
- ✅ WiFi persistence fixed
- ✅ Web server stability improved
- ✅ JSON buffer overflow fixed
- ✅ Config portal working
- ✅ 2.4GHz WiFi restriction applied
