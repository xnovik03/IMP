
---

````markdown
# Smart Home Panel with ESP32 and MQTT

This project implements a control panel for a smart home using an **ESP32** microcontroller and an **SSD1306 OLED** display. The system allows users to read the current state of rooms (light, temperature) and control devices through physical buttons. Communication is handled via the **MQTT protocol**.

Developed as part of the *Microprocessor and Embedded Systems* course at **Brno University of Technology**.

## Features

- OLED interface for room overview and control
- Button-based navigation (SELECT and OK)
- MQTT communication for sending and receiving room data
- Room state tracking (light ON/OFF, temperature increase)

## Hardware

- **ESP32 board**
- **SSD1306 OLED display (SPI)**
- **Buttons**:
  - SELECT: navigate
  - OK: confirm

### Pin Mapping

| Function | GPIO |
|---------|------|
| MOSI    | 23   |
| SCLK    | 18   |
| CS      | 5    |
| DC      | 27   |
| RESET   | 17   |
| SELECT  | 34   |
| OK      | 35   |

## Application Structure

- **Main menu**: choose between status view and control
- **Room overview**: displays all room data
- **Control screen**: toggle light, increase temperature

## MQTT Topics

- Published topics: `home/room/{room_number}`
- Example message:
  ```json
  {"light": 1, "temperature": 18}
````

Tested using:

```bash
mosquitto_sub -h mqtt.eclipseprojects.io -t "home/room/#" -v
```

## How to Run the Project

1. **Clone the repository** and open it in [PlatformIO](https://platformio.org/) (VS Code).
2. **Add your Wi-Fi credentials** in the `my_data.h` file:

   ```c
   #define WIFI_SSID "your-ssid"
   #define WIFI_PASS "your-password"
   ```
3. **Connect the ESP32** to your computer via USB.
4. **Build and upload** the project:

   ```bash
   pio run --target upload
   ```
5. **Monitor output** (optional):

   ```bash
   pio device monitor
   ```

Make sure your MQTT broker is running or use a public one like `mqtt.eclipseprojects.io`.

## Files

* `main.c` – main logic (UI, MQTT)
* `my_data.h` – Wi-Fi credentials
* `components/` – SSD1306 display library
* `config/` – ESP-IDF settings
* `CMakeLists.txt` – build configuration

## Tools Used

* PlatformIO (VS Code)
* ESP-IDF
* Mosquitto MQTT broker
* SSD1306 OLED library (from nopnop2002)


