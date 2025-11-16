# ESP32 Uptime Monitor - PlatformIO Usage

This project has been configured for PlatformIO. Follow the instructions below to build and upload the firmware to your ESP32-WROOM-32U device.

## Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) or [PlatformIO IDE](https://platformio.org/platformio-ide) (VS Code extension)
- ESP32-WROOM-32U device connected via USB

## Project Structure

```
ESP32-Uptime-Monitor/
├── platformio.ini      # PlatformIO configuration
├── src/
│   └── main.cpp        # Main source code
├── lib/                # Custom libraries (if needed)
└── include/            # Header files (if needed)
```

## Building the Project

### Using PlatformIO CLI

1. Open a terminal in the project directory
2. Build the project:
   ```bash
   pio run
   ```

### Using PlatformIO IDE (VS Code)

1. Open the project folder in VS Code
2. Click the checkmark icon (✓) in the PlatformIO toolbar to build

## Uploading to ESP32

### Using PlatformIO CLI

1. Connect your ESP32-WROOM-32U via USB
2. Upload the firmware:
   ```bash
   pio run --target upload
   ```

### Using PlatformIO IDE (VS Code)

1. Connect your ESP32-WROOM-32U via USB
2. Click the right arrow icon (→) in the PlatformIO toolbar to upload

## Monitoring Serial Output

### Using PlatformIO CLI

```bash
pio device monitor
```

### Using PlatformIO IDE (VS Code)

Click the plug icon in the PlatformIO toolbar

## Configuration

The device uses the following default WiFi credentials (can be changed via web interface after first boot):
- SSID: `hotspot123321`
- Password: `pw123456`

## Troubleshooting

### Port Not Found

If PlatformIO can't find your device:
```bash
# List available serial ports
pio device list

# Specify port manually
pio run --target upload --upload-port /dev/ttyUSB0
```

### Upload Speed Issues

If uploads fail, try reducing the upload speed by editing `platformio.ini`:
```ini
upload_speed = 115200
```

### Reset to Factory Defaults

Hold the BOOT button on your ESP32 for 5 seconds during startup to reset all settings to default values.

## Libraries Used

- ESP Async WebServer (^1.2.3)
- AsyncTCP (^1.1.1)
- ArduinoJson (^6.21.3)

## Additional Commands

- Clean build files: `pio run --target clean`
- Update libraries: `pio pkg update`
- Install dependencies: `pio pkg install`
