# Changelog

## Original Project Attribution

This project is based on [ESP32-Uptime-Monitor](https://github.com/PearXP/esp32-web-server-status) by PearXP.

Version 13 represents a complete architectural rewrite and redesign with significant enhancements:
- Converted from Arduino .ino to PlatformIO project structure
- Complete UI/UX redesign with modern interface
- Expanded from 3 to 20 server monitoring capacity
- New RESTful API endpoints for full CRUD operations
- Migration from EEPROM to LittleFS JSON storage
- Dynamic group-based server organization

While the core monitoring concept is preserved, the implementation, architecture, and feature set have been substantially expanded and modernized.

---

## Version 13 (2025-11-16) - Major UI Redesign & Scalability Improvements

### 🎨 Frontend Redesign
- **Complete UI overhaul** with modern, clean interface
- **Group-based organization** - Organize servers into custom groups (Production, Staging, etc.)
- **Tab navigation** with server count badges
- **Table layout** - Clean list view showing: Name, Status, URL, Ping, Actions
- **Expandable rows** - Click any row to reveal detailed properties inline (no modals)
- **Floating "+ Add Server" button** for easy access to add new servers
- **Real-time updates** - Auto-refresh every 5 seconds

### 🚀 Backend Enhancements
- **Increased capacity** from 3 to 20 server monitoring slots
- **Storage migration** from EEPROM to LittleFS JSON for flexible configuration
- **New data structure** with `group_name` and `enabled` fields
- **Dynamic grouping** - Groups created automatically based on server configurations

### 🔧 New API Endpoints
- `GET /api/groups` - List all unique groups
- `POST /api/server/add` - Add new server with full configuration
- `POST /api/server/delete` - Delete/disable server
- `POST /api/server/update` - Update server configuration
- `POST /api/group/rename` - Rename group across all servers
- Updated `/api/status` - Now includes server ID, group name, and enabled status

### ✨ Features
- **Server management UI** - Edit and Delete buttons for each server
- **Inline details view** - Displays group, check interval, ping stats, thresholds, and uptime timeline
- **Dynamic configuration** - Add, edit, or remove servers without recompiling
- **Empty state handling** - Helpful prompts when no servers are configured
- **Improved settings modal** - Simplified general settings (WiFi managed via WiFiManager)

### 🐛 Bug Fixes
- Fixed forward declaration issue for `saveConfig()`
- Optimized memory usage for 20+ servers
- Enhanced JSON buffer sizes for larger configurations

### 📊 Technical Details
- **Firmware Version**: 13
- **Config Storage**: LittleFS (`/config.json`)
- **JSON Buffer**: 10KB (10240 bytes) for 20 servers
- **Flash Usage**: 89.6% (1,175,041 bytes)
- **RAM Usage**: 28.5% (93,448 bytes)

### 🔄 Migration Notes
- Existing configurations are automatically migrated from EEPROM to LittleFS
- Config version bumped to 13 to trigger proper migration
- First 3 servers assigned to "Production" group by default
- Remaining slots assigned to "Staging" group
- Only enabled servers are saved to reduce config file size

### 📝 Breaking Changes
- Configuration storage format changed from EEPROM to JSON
- WiFi credentials now managed exclusively via WiFiManager (no hardcoded values)
- General settings simplified (removed per-server tabs from settings modal)

---

## Version 12 (Previous)
- Arduino-based .ino implementation
- 3 server monitoring slots
- Hardcoded WiFi credentials
- Individual tabs for each server
