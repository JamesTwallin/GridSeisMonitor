#!/usr/bin/env python3
"""
GridSeis Data Capture - Auto-detects ESP32 and logs frequency data.
"""

import serial
import serial.tools.list_ports
import json
from datetime import datetime


def find_esp32():
    """Find ESP32 serial port automatically."""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        # Look for common ESP32 USB chips
        desc = (port.description or '').lower()
        if any(chip in desc for chip in ['cp210', 'ch340', 'usb serial', 'uart']):
            return port.device
    # Fallback: return first available port
    if ports:
        return ports[0].device
    return None


def main():
    port = find_esp32()
    if not port:
        print("No ESP32 found. Available ports:")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device}: {p.description}")
        return

    output_file = f"grid_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl"

    print(f"Connecting to {port}...")
    ser = serial.Serial(port, 115200, timeout=1)
    print(f"Logging to {output_file}")
    print("Press Ctrl+C to stop.\n")

    with open(output_file, 'w') as f:
        try:
            while True:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line.startswith('{') and line.endswith('}'):
                    try:
                        data = json.loads(line)
                        data['wall_time'] = datetime.now().isoformat()
                        f.write(json.dumps(data) + '\n')
                        f.flush()
                        print(f"{data['freq']:.4f} Hz | signal: {data['signal']:.3f}")
                    except (json.JSONDecodeError, KeyError):
                        pass
        except KeyboardInterrupt:
            print("\nDone.")


if __name__ == '__main__':
    main()
