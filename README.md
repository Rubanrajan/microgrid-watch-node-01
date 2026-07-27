# Microgrid Watch — Node 01

An IoT-based solar microgrid monitoring system for a 12V solar panel + lead-acid battery setup. Built as a student/hobby project for real-time monitoring of energy generation, storage, and consumption, with a web dashboard for community operators.

## System overview

- **Hardware**: ESP32-WROOM-32, three INA219 current/voltage sensors (panel, battery, load), SSD1306 OLED, PWM charge controller, 12V relay (load shedding), active buzzer (undervoltage/overcharge alerts), LM2596 5V buck converter, DC motor load.
- **Firmware**: Arduino sketch reading all three INA219 sensors, publishing JSON telemetry to HiveMQ Cloud over MQTT/TLS (port 8883), with local load-shed and buzzer protection logic, plus a fallback local web page served directly from the ESP32 if the broker is unreachable.
- **Dashboard**: Standalone HTML page with live sparklines, a battery gauge, daily energy totals, configurable alert thresholds, and light/dark themes. Connects to the broker over MQTT/WebSocket (port 8884), or can be served from the ESP32 itself for local polling.

## Repo structure

```
firmware/    ESP32 Arduino sketch (.ino)
dashboard/   Standalone web dashboard (index.html)
```

## Getting started

### Firmware
1. Open `firmware/esp32_microgrid_node.ino` in the Arduino IDE.
2. Install libraries: `PubSubClient`, `Adafruit INA219`, `ArduinoJson`.
3. Fill in your WiFi and HiveMQ Cloud credentials at the top of the file.
4. Flash to an ESP32-WROOM-32.

### Dashboard
Open `dashboard/index.html` in a browser (serve it via a local web server, not `file://`, so WebSocket connections work), and connect it to your HiveMQ Cloud WebSocket endpoint (port 8884), or point it at the ESP32's local IP if using the local polling fallback.

## Notes

- The firmware currently uses `setInsecure()` for TLS as a beginner-friendly default — swap in the ISRG Root X1 certificate via `setCACert()` before any production/public deployment.
- INA219 sensors must be wired in series for accurate current sensing.
- I2C addresses: panel `0x41`, battery `0x40`, load `0x44` — resolve conflicts via A0/A1 solder pads or a TCA9548A multiplexer.

## License

MIT — see [LICENSE](LICENSE).
