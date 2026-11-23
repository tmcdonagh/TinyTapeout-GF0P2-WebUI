### Task 1: Pico project scaffold + serial helper

**Files:**
- Create: `pico/platformio.ini`, `pico/src/main.cpp` (minimal), `pico/tools/serctl.py`
- Modify: nothing else. The root ESP32 project is untouched.

**Interfaces:**
- Produces: a buildable/flashable `pico/` project and `python3 pico/tools/serctl.py "<command>"` which prints the Pico's serial response (auto-detects the first `2e8a:` USB CDC port, 115200 baud, ~2 s settle timeout). Later tasks use it for every serial verification.

- [ ] **Step 1: Create `pico/platformio.ini`**

```ini
[env:pico2w]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = rpipico2w
framework = arduino
board_build.core = earlephilhower
board_build.filesystem_size = 0m
monitor_speed = 115200
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0
```

- [ ] **Step 2: Write minimal `pico/src/main.cpp`**

`setup()`: `Serial.begin(115200)` then print `Pico TT showcase scaffold` on a 1 s interval in `loop()` (heartbeat for flash/serial verification).

- [ ] **Step 3: Write `pico/tools/serctl.py`**

pyserial helper: find port by `serial.tools.list_ports` VID `0x2E8A`; open 115200; send the given line + `\n`; stream response lines until 1 s of silence; exit code 0. Host setup: `python3 -m pip install --user pyserial` if missing.

- [ ] **Step 4: Build**

Run: `pio run -d pico`
Expected: `SUCCESS` (first run downloads the platform/core; takes a while — let it finish).

- [ ] **Step 5: Flash**

Ask Tom to hold **BOOTSEL** while plugging the Pico in (first flash only), confirm `/dev/ttyACM*` appears (usbipd attach from Windows if needed — `wsl2-usb-serial` skill).
Run: `pio run -d pico -t upload`
Expected: upload succeeds via picotool; subsequent flashes auto-reboot (if auto-reboot flakes: BOOTSEL again — known arduino-pico/PlatformIO quirk).
Run: `python3 pico/tools/serctl.py ""`
Expected: heartbeat line printed.

- [ ] **Step 6: Suggest commit**

Suggest to Tom: `feat: scaffold Pico 2 W PlatformIO project and serial helper`

