# Developer build and flash

If you want to modify the firmware, build it from source with PlatformIO.

## One-time setup on Windows (WSL2)

1. Install `usbipd-win` if you don't have it:

   ```powershell
   winget install usbipd
   ```

2. Plug the FireBeetle into USB-C. In PowerShell, list devices:

   ```powershell
   usbipd list
   ```

   Note the BUSID (e.g., `1-7`).

3. Bind the device once:

   ```powershell
   usbipd bind --busid <BUSID>
   ```

## Per-session attach

1. Attach the device to WSL2:

   ```powershell
   usbipd attach --wsl --busid <BUSID>
   ```

2. In WSL2, verify the serial port appeared:

   ```bash
   ls -l /dev/ttyUSB* /dev/ttyACM*
   ```

## Build and flash

```bash
cd TinyTapeout-GF0P2-WebUI
pio run --target upload --upload-port /dev/ttyUSB0
```

## Monitor

```bash
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

The Serial output will show the AP name and IP address (`192.168.4.1`).
