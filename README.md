# GridSeis - DIY Grid Frequency Monitor

Build your own real-time power grid frequency monitor using an ESP32 and a servo motor. Watch the needle move as the UK grid fluctuates around 50Hz.

**No external sensors required** - the ESP32 picks up the 50Hz electromagnetic field from nearby mains wiring using just a short wire antenna (or even nothing at all).

## What You'll Build

A physical dial that shows the real-time frequency of your power grid:
- **Centre position** = 50.00 Hz (nominal)
- **Left** = frequency dropping (high demand on grid)
- **Right** = frequency rising (excess generation)

Grid frequency is a fascinating indicator of supply/demand balance across an entire electrical network.

## Parts List

| Part | Approx. Cost | Notes |
|------|--------------|-------|
| ESP32 DevKit | £5-10 | Any ESP32 board works (ESP32, ESP32-S2, ESP32-S3) |
| MG996R Servo | £5-8 | Or any standard hobby servo |
| Jumper wires | £2 | 3 wires for servo, 1 optional for antenna |
| USB cable | - | For power and programming |

**Total: ~£12-20**

## Wiring

```
ESP32                MG996R Servo
─────                ────────────
GPIO 4  ──────────►  Orange (Signal)
5V      ──────────►  Red (Power)
GND     ──────────►  Brown (Ground)

GPIO 34: Leave floating or attach 10-30cm wire as antenna
```

### Wiring Diagram

```
                    ┌─────────────────┐
                    │     ESP32       │
                    │                 │
    ┌───────────────┤ GPIO 4          │
    │               │                 │
    │   ┌───────────┤ 5V              │
    │   │           │                 │
    │   │   ┌───────┤ GND             │
    │   │   │       │                 │
    │   │   │       │ GPIO 34 ────────┼──── Optional: 10-30cm wire antenna (a dupont wire works fine!)
    │   │   │       │                 │
    │   │   │       └─────────────────┘
    │   │   │
    │   │   │       ┌─────────────────┐
    │   │   │       │  MG996R Servo   │
    │   │   │       │                 │
    └───┼───┼───────┤ Orange (Signal) │
        │   │       │                 │
        └───┼───────┤ Red (Power)     │
            │       │                 │
            └───────┤ Brown (Ground)  │
                    │                 │
                    └─────────────────┘
```

## Software Setup

### 1. Install VS Code + PlatformIO

PlatformIO handles all the toolchain setup automatically - no need to install anything else.

1. **Install VS Code**: Download from https://code.visualstudio.com/

2. **Install PlatformIO Extension**:
   - Open VS Code
   - Go to Extensions (Ctrl+Shift+X / Cmd+Shift+X on Mac)
   - Search "PlatformIO IDE"
   - Click Install
   - Wait for it to finish (it downloads compilers etc.)

### 2. Get the Code

```bash
git clone https://github.com/yourusername/GridSeisMonitor.git
```

Or download as ZIP from GitHub and extract.

### 3. Open in VS Code

- File → Open Folder → select the GridSeisMonitor folder
- VS Code will ask if you trust the folder - click Yes
- Wait for PlatformIO to initialise (first time takes a few minutes)

### 4. Build and Upload

1. Connect your ESP32 via USB
2. Click the **→** (right arrow) icon in the blue status bar at the bottom
   - Or use the keyboard shortcut: Ctrl+Alt+U
3. PlatformIO will auto-detect your board and upload

### 5. View Output

- Click the **plug icon** in the bottom toolbar to open Serial Monitor
- Or press Ctrl+Alt+S
- You should see frequency readings like: `{"t":12345,"freq":50.0123,...}`

## How It Works

1. **Signal Pickup**: The ESP32's ADC samples electromagnetic interference from nearby mains wiring at 1000 Hz. Even a floating GPIO pin picks up the 50Hz hum.

2. **IQ Demodulation**: The firmware correlates the signal with reference 50Hz sine and cosine waves to extract the phase.

3. **Frequency Calculation**: By measuring how the phase drifts between 1-second windows, we calculate the actual grid frequency to ~0.001 Hz precision.

4. **Servo Display**: The frequency is mapped to a servo angle, giving you a physical dial that responds in real-time.

## Data Logging (Optional)

The ESP32 outputs JSON data over serial that you can log and plot.

### Install Python Dependencies

```bash
pip install -r requirements.txt
```

### Capture Data

```bash
# List available serial ports
python capture.py --list

# Capture from one board
python capture.py COM3

# Capture from multiple boards
python capture.py COM3 COM4

# On Linux/Mac
python capture.py /dev/ttyUSB0
```

### Plot Data

```bash
python plotter.py
```

This creates a graph comparing your ESP32 readings against official grid data (if available).

### Compare with Official Grid Data

UK grid frequency data is available at:
https://www.nationalgrideso.com/data-portal/system-frequency-data

Download the "Rolling System Frequency" CSV/JSON and place it in the project folder.

## Serial Output Format

The ESP32 outputs JSON lines at 115200 baud:

```json
{"t":12345,"freq":50.0123,"smoothed":50.0100,"signal":0.543}
```

| Field | Description |
|-------|-------------|
| `t` | Timestamp (ms since boot) |
| `freq` | Instantaneous frequency (Hz) |
| `smoothed` | Exponentially smoothed frequency |
| `signal` | Signal amplitude (0-1, higher = stronger pickup) |

## USB Drivers

Your ESP32 board has a USB-to-serial chip that needs a driver. **If your board isn't detected, this is usually why.**

### Which chip do I have?

Look at the small chip near the USB port on your board:
- **CP2102** or **CP2104** (square, says "SILABS") → Need CP210x driver
- **CH340** or **CH341** (rectangular, says "CH340") → Need CH340 driver
- **No chip visible** (ESP32-S2/S3 with native USB) → No driver needed

### Download Links

| Chip | Windows | Mac | Linux |
|------|---------|-----|-------|
| CP210x | [Silicon Labs](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) | [Silicon Labs](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) | Built-in |
| CH340 | [WCH](http://www.wch-ic.com/downloads/CH341SER_ZIP.html) | [WCH](http://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html) | Built-in |

### After Installing

1. Unplug and replug your ESP32
2. Check it appears:
   - **Windows**: Device Manager → Ports (COM & LPT) → should show "Silicon Labs CP210x" or "USB-SERIAL CH340"
   - **Mac**: Terminal → `ls /dev/tty.usb*`
   - **Linux**: Terminal → `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`

## Troubleshooting

### Servo doesn't move
- Check wiring (especially the signal wire to GPIO 4)
- Ensure servo is getting 5V (some USB ports can't supply enough current)
- Try a powered USB hub or external 5V supply for the servo

### Readings are unstable
- Move closer to mains wiring (near a wall socket or power strip)
- Try attaching a 10-30cm wire antenna to GPIO 34
- Ensure good grounding

### Frequency seems wrong
- The measurement needs ~10 seconds to stabilise after power-on
- Check you're in a 50Hz country (UK, Europe, etc.)

### Can't upload to ESP32
- Check USB drivers are installed (see USB Drivers section above)
- Try a different USB cable (some are charge-only, with no data wires)
- Hold the **BOOT** button while clicking upload, release after "Connecting..." appears
- Some boards need you to hold BOOT, press EN/RST, then release both

## Project Structure

```
GridSeisMonitor/
├── main/
│   └── main.c           # Firmware source code
├── capture.py           # Python script to log serial data
├── plotter.py           # Python script to plot frequency data
├── platformio.ini       # PlatformIO build configuration
└── requirements.txt     # Python dependencies
```

## Contributing

Pull requests welcome! Some ideas:
- Support for different servo types
- OLED/LCD display option
- WiFi data upload
- Web dashboard
- Multiple antenna correlation for better accuracy

## Acknowledgements

- Grid frequency data from [National Grid ESO](https://www.nationalgrideso.com/)
- Inspired by various grid frequency monitoring projects

## License

MIT License - do whatever you want with this code.
