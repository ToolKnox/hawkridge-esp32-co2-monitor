# Installation and build guide

## Project

ESP32-C3 CO2 Monitor Enclosure

MIT ESP32-C3 air-quality monitor with SCD41, BMP180, LCD/OLED display notes, firmware, wiring, and 3D printed enclosure files.

## Quick start

1. Download the printable files from Printables: https://www.printables.com/model/1776629-esp32-c3-co2-monitor-enclosure
2. Download this GitHub repository as a ZIP, or clone it:

   ```bash
   git clone https://github.com/ToolKnox/hawkridge-esp32-co2-monitor.git
   ```

3. Use `source/upstream/` for the mirrored software, firmware, PCB, CAD-source, and upstream documentation.
4. Use the Bill of Material PDF attached to the Printables Documentation section for parts planning.
5. Read the project-specific notes below before powering electronics or uploading firmware.

## Software / firmware setup

- Install the ESP32 board support in Arduino IDE or PlatformIO before uploading firmware.
- PCB/Gerber/electronics design files are mirrored under `source/upstream/`; open them with the original toolchain noted by the file type, such as KiCad, Altium, or CAM/Gerber viewers.

## Main software/config files

- `source/upstream/data/cfg/cfgap.json`
- `source/upstream/data/cfg/cfgnetwork.json`
- `source/upstream/data/cfg/cfgpins.json`
- `source/upstream/data/cfg/cfgwifi.json`
- `source/upstream/data/html/deps/bms.min.js`
- `source/upstream/platformio.ini`
- `source/upstream/src/DeviceDiagnostics.h`
- `source/upstream/src/Display.h`
- `source/upstream/src/Network.cpp`
- `source/upstream/src/Network.h`
- `source/upstream/src/REST.h`
- `source/upstream/src/Sensor.h`
- `source/upstream/src/config.cpp`
- `source/upstream/src/config.h`
- `source/upstream/src/main.cpp`

## PCB / electronics design files

- `source/upstream/custom_partition.csv`

## Upstream documentation mirrored here

- `source/upstream/README.md`

## Original source

https://github.com/intuibase/esp32_co2_monitor
