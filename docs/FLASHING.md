# Flashing the firmware

The easiest way to try the firmware is to download the pre-built binaries from a [GitHub release](https://github.com/tmcdonagh/TinyTapeout-GF0P2-WebUI/releases) and flash them with `esptool.py`.

If you want to build and flash from source instead, see [`DEVELOPER.md`](DEVELOPER.md).

## What you need

- A FireBeetle 2 ESP32-E (DFR0654).
- A USB-C cable.
- Python 3 with `esptool` installed:

  ```bash
  pip install esptool
  ```

## Flash

1. Download `firmware.bin`, `bootloader.bin`, and `partitions.bin` from the latest release.
2. Plug in the FireBeetle and find its serial port:

   - **Linux/WSL2:** `/dev/ttyUSB0` or `/dev/ttyACM0`
   - **macOS:** `/dev/cu.usbserial-*`
   - **Windows:** `COM3` or similar

3. Erase and flash:

   ```bash
   esptool.py --chip esp32 --port /dev/ttyUSB0 erase_flash
   esptool.py --chip esp32 --port /dev/ttyUSB0 \
     --baud 921600 \
     write_flash -z \
     0x1000 bootloader.bin \
     0x8000 partitions.bin \
     0x10000 firmware.bin
   ```

   On Windows, replace `/dev/ttyUSB0` with your COM port, e.g., `--port COM3`.

4. Reset the board. It will create the WiFi network `TT-GF0P2-Showcase`.
5. Connect with password `tinytapeout` and open `http://192.168.4.1/` in a browser.
