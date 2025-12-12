#!/usr/bin/env python3
"""
GridSeis Data Capture

Logs frequency measurements from ESP32 boards to JSONL files.
Run with --help to see available options.
"""

import serial
import serial.tools.list_ports
import json
import argparse
from datetime import datetime
import threading
import sys


def list_serial_ports():
    """List available serial ports."""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
        return []
    print("Available serial ports:")
    for port in ports:
        print(f"  {port.device}: {port.description}")
    return [p.device for p in ports]


def capture(port, name, output_dir="."):
    """Capture data from a single ESP32 board."""
    output_file = f"{output_dir}/grid_log_{name}.jsonl"
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"[{name}] Connected to {port}")
        print(f"[{name}] Logging to {output_file}")

        with open(output_file, 'a') as f:
            while True:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line.startswith('{') and line.endswith('}'):
                        data = json.loads(line)
                        data['board'] = name
                        data['wall_time'] = datetime.now().isoformat()
                        f.write(json.dumps(data) + '\n')
                        f.flush()
                        print(f"[{name}] {data['freq']:.4f} Hz | signal: {data['signal']:.3f}")
                except json.JSONDecodeError:
                    pass  # Skip malformed lines
                except KeyboardInterrupt:
                    break
    except serial.SerialException as e:
        print(f"[{name}] Serial error: {e}")
    except Exception as e:
        print(f"[{name}] Error: {e}")


def main():
    parser = argparse.ArgumentParser(
        description="Capture grid frequency data from ESP32 boards",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python capture.py --list                    # List available serial ports
  python capture.py COM3                      # Capture from single board
  python capture.py COM3 COM4                 # Capture from multiple boards
  python capture.py /dev/ttyUSB0 --name esp1  # Custom board name
        """
    )
    parser.add_argument('ports', nargs='*', help='Serial port(s) to capture from (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('--list', '-l', action='store_true', help='List available serial ports and exit')
    parser.add_argument('--name', '-n', help='Board name (for single port only)')
    parser.add_argument('--output', '-o', default='.', help='Output directory for log files')

    args = parser.parse_args()

    if args.list:
        list_serial_ports()
        return

    if not args.ports:
        print("No ports specified. Use --list to see available ports.\n")
        parser.print_help()
        return

    threads = []
    for i, port in enumerate(args.ports):
        name = args.name if (args.name and len(args.ports) == 1) else f"board{i+1}"
        t = threading.Thread(target=capture, args=(port, name, args.output), daemon=True)
        t.start()
        threads.append(t)

    print(f"\nCapturing from {len(args.ports)} board(s)... Press Ctrl+C to stop.\n")

    try:
        while any(t.is_alive() for t in threads):
            for t in threads:
                t.join(timeout=0.5)
    except KeyboardInterrupt:
        print("\nStopping capture...")


if __name__ == '__main__':
    main()
