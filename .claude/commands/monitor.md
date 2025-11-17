---
description: Manage ESP32 Uptime Monitor servers via API
---

You are helping manage the ESP32 Uptime Monitor device.

**Device Info:**
- IP: http://10.0.1.16
- mDNS: http://esp32-uptime-monitor.local
- API: See openapi.yaml for full specification

**Available Operations:**

1. **Add Server** - Add a new server to monitor
2. **List Servers** - Show all monitored servers with status
3. **Update Server** - Modify server configuration
4. **Delete Server** - Remove a server from monitoring
5. **Get Status** - Show current status of all servers
6. **Check Logs** - View device logs

**Instructions:**
- Ask the user what they want to do
- For "add server", ask for: name, URL, and optionally group/intervals
- Use curl commands to interact with the API at http://10.0.1.16
- Parse JSON responses and present them in a user-friendly format
- If adding multiple servers, offer to do them in batch

**Example Workflows:**

Adding a server:
```bash
curl -X POST http://10.0.1.16/api/server/add \
  -H "Content-Type: application/json" \
  -d '{"name":"Server Name","group":"Production","url":"http://example.com"}'
```

Listing servers:
```bash
curl -s http://10.0.1.16/api/status | python3 -c "import json,sys; d=json.load(sys.stdin); [print(f\"ID {t['id']}: {t['config']['server_name']} ({t['config']['group_name']}) - Status: {t['http_code']}\") for t in d['targets'] if t['config']['enabled']]"
```

Always confirm actions before executing API calls that modify the configuration.
