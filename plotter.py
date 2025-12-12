import json
from datetime import datetime, timedelta
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from pathlib import Path
import glob
import sys
import numpy as np

def load_grid_reference(json_path):
    """Load National Grid reference frequency data from JSON file."""
    with open(json_path, 'r') as f:
        data = json.load(f)
    times = [datetime.fromisoformat(d['measurementTime'].replace('Z', '+00:00')) for d in data]
    freqs = [d['frequency'] for d in data]
    return times, freqs

def load_esp32_log(log_file, time_offset_hours=0, invert_freq=True, use_smoothed=False):
    """Load ESP32 captured frequency data from JSONL file."""
    times, freqs, board = [], [], None
    offset = timedelta(hours=time_offset_hours)
    freq_key = 'smoothed' if use_smoothed else 'freq'
    with open(log_file, 'r') as f:
        for line in f:
            try:
                d = json.loads(line.strip())
                if 'wall_time' in d and freq_key in d:
                    times.append(datetime.fromisoformat(d['wall_time']) + offset)
                    freq = d[freq_key]
                    if invert_freq:
                        # Invert around 50Hz: 50.1 -> 49.9, 49.9 -> 50.1
                        freq = 100.0 - freq
                    freqs.append(freq)
                    board = d.get('board', Path(log_file).stem)
            except json.JSONDecodeError:
                continue
    return times, freqs, board

def find_optimal_offset(ref_times, ref_freqs, esp_times, esp_freqs):
    """Find optimal time offset using cross-correlation."""
    # Resample both to common 15-second intervals
    ref_start = min(ref_times)
    ref_end = max(ref_times)

    # Try offsets from -2 hours to +2 hours in 1-minute steps
    best_offset = 0
    best_corr = -999

    for offset_min in range(-120, 121, 1):
        offset = timedelta(minutes=offset_min)
        shifted_esp_times = [t + offset for t in esp_times]

        # Find overlapping region
        overlap_start = max(ref_start, min(shifted_esp_times))
        overlap_end = min(ref_end, max(shifted_esp_times))

        if overlap_start >= overlap_end:
            continue

        # Resample to common times (15-sec intervals)
        common_times = []
        t = overlap_start
        while t <= overlap_end:
            common_times.append(t)
            t += timedelta(seconds=15)

        if len(common_times) < 10:
            continue

        # Interpolate both signals
        ref_interp = np.interp(
            [t.timestamp() for t in common_times],
            [t.timestamp() for t in ref_times],
            ref_freqs
        )
        esp_interp = np.interp(
            [t.timestamp() for t in common_times],
            [t.timestamp() for t in shifted_esp_times],
            esp_freqs
        )

        # Compute correlation
        corr = np.corrcoef(ref_interp, esp_interp)[0, 1]
        if corr > best_corr:
            best_corr = corr
            best_offset = offset_min

    print(f"Optimal offset: {best_offset} minutes (correlation: {best_corr:.4f})")
    return best_offset

def plot_grid_frequency(reference_json=None, log_files=None):
    """Plot grid frequency data from reference JSON and/or captured logs."""
    fig, ax = plt.subplots(figsize=(12, 6))

    # Load reference data
    ref_times, ref_freqs = None, None
    if reference_json:
        ref_times, ref_freqs = load_grid_reference(reference_json)
        ax.plot(ref_times, ref_freqs, 'b-', linewidth=1.5, label='National Grid Reference', alpha=0.8)
        print(f"Loaded {len(ref_freqs)} reference samples from {reference_json}")
        print(f"  Time range: {ref_times[0]} to {ref_times[-1]}")
        print(f"  Frequency range: {min(ref_freqs):.3f} - {max(ref_freqs):.3f} Hz")

    # Load and plot captured ESP32 data
    if log_files:
        for log_file in log_files:
            esp_times, esp_freqs, board = load_esp32_log(log_file, time_offset_hours=0, invert_freq=True)
            if not esp_times:
                continue

            ax.plot(esp_times, esp_freqs, '.', markersize=2, label=f'ESP32: {board}', alpha=0.6)
            print(f"Loaded {len(esp_freqs)} samples from {log_file}")

    ax.axhline(y=50.0, color='gray', linestyle='--', alpha=0.5, label='50 Hz nominal')
    ax.set_xlabel('Time (UTC)')
    ax.set_ylabel('Frequency (Hz)')
    ax.set_title('UK Grid Frequency')
    ax.legend(loc='best')
    ax.grid(True, alpha=0.3)
    ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
    ax.xaxis.set_major_locator(mdates.MinuteLocator(interval=5))

    # Truncate to 15:23-15:58 UTC
    ax.set_xlim(datetime(2025, 12, 10, 15, 23, tzinfo=ref_times[0].tzinfo if ref_times else None),
                datetime(2025, 12, 10, 15, 58, tzinfo=ref_times[0].tzinfo if ref_times else None))
    fig.autofmt_xdate()

    # Save plot
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    output_path = f'grid_plot_{timestamp}.png'
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Plot saved to {output_path}")
    plt.show()
    return output_path

if __name__ == '__main__':
    # Find reference JSON files (National Grid data)
    reference_files = glob.glob('RollingSystemFrequency*.json')
    reference_json = reference_files[0] if reference_files else None

    # Find ESP32 log files
    log_files = glob.glob('grid_log_*.jsonl')

    if not reference_json and not log_files:
        print("No data files found. Place RollingSystemFrequency*.json or grid_log_*.jsonl files in this directory.")
        sys.exit(1)

    plot_grid_frequency(reference_json=reference_json, log_files=log_files if log_files else None)
