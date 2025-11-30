# ESP32 Pet Temperature Monitor with Discord Alerts

This project is an ESP32-based pet comfort monitor that measures ambient temperature, shows it on an OLED screen, and sends alerts to a Discord channel when the environment is too hot or too cold for a selected pet.

The whole device is controlled with an IR remote, including Wi-Fi setup and text entry on a custom on-screen keyboard.

## Features

- **ESP32 + DHT11** temperature sensor
- **0.96" SSD1306 OLED** display (128x64, I²C)
- **IR remote control** with startup calibration
- **On-screen QWERTY keyboard** for entering Wi-Fi passwords and webhook details
- **Wi-Fi scanner** to choose an SSID from nearby networks
- **NTP local time** display (configurable time zone offset in code)
- **Pet profiles** with realistic temperature ranges:
  - Dog  
  - Cat  
  - Rabbit  
  - Reptile (generic warm-climate profile)
- **Automatic alerts** when the room is too hot or too cold for the chosen pet:
  - Status shown on the OLED (OK / HOT! / COLD!)
  - **Discord webhook notification** sent with the current temperature
- **Settings menu** (all navigated with the IR remote):
  - Switch °C / °F
  - Wi-Fi setup (scan + password)
  - Choose pet type
  - Configure Discord webhook ID and token
  - Send a test message to the webhook

> This is a hobby project, not a medical or safety device.  
> Temperature ranges are simple comfort guidelines and should not replace proper care or professional advice.

## Hardware

- ESP32 development board
- DHT11 temperature and humidity sensor
- SSD1306 0.96" OLED display (I²C, 128x64)
- IR receiver module (connected to GPIO 15)
- Generic IR remote control
- Breadboard and jumper wires

### Pin connections (ESP32)

- **DHT11 data** → GPIO 4  
- **OLED SDA** → GPIO 21  
- **OLED SCL** → GPIO 22  
- **IR receiver OUT** → GPIO 15  
- 3.3V and GND to all modules as required

## Software setup

1. Install the **Arduino IDE** and ESP32 board support.
2. Install the following libraries from Library Manager:
   - `Adafruit SSD1306`
   - `Adafruit GFX`
   - `DHT sensor library`
   - `IRremote` (v3+, using `IRremote.hpp`)
3. Open the sketch and adjust:
   - `gmtOffset_sec` to match your local time zone
   - Optional: default `webhookId` and `webhookToken` if you want them set in code
4. Create a Discord webhook in the server/channel of your choice and copy:
   - The numeric **ID** part
   - The **token** part  
   (These can also be entered on the device via the settings menu.)
5. Upload the sketch to the ESP32.

## How to use

1. **IR calibration**  
   On first boot the device asks you to press the UP, DOWN, LEFT, RIGHT, OK, and BACK buttons on your remote. These codes are stored for this session.

2. **Connect Wi-Fi**  
   - Go to **Settings → WiFi setup**.  
   - The ESP32 scans for nearby networks; select your SSID with the remote.  
   - Enter the password with the on-screen keyboard (Shift toggles upper/lower case).  
   - The device connects to Wi-Fi and syncs time via NTP.

3. **Configure Discord webhook**  
   - Go to **Settings → Webhook setup**.  
   - Enter the webhook **ID**, then the **token** using the keyboard.  
   - Use **Webhook test** to send a test message to your Discord channel.

4. **Choose pet profile**  
   - Go to **Settings → Pet type** and pick Dog, Cat, Rabbit, or Reptile.  
   - The screen also shows the temperature range in °C and °F for that pet.

5. **Monitoring**  
   - The main screen shows:
     - Current temperature (°C or °F)
     - Local time
     - Selected pet and status (OK / HOT! / COLD!)
     - Wi-Fi status and IP address
   - If the temperature goes outside the selected pet’s comfort range, the status changes and a Discord alert is sent.

## License

MIT License – feel free to use, modify, and learn from this project.
