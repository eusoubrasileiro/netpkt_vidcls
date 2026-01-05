#!/usr/bin/env python3
"""
Test the NaiveTracker against all HDF5 training data scenarios.

Loads the training data from training/raw.h5, replays each labeled scenario
through the naive tracker, and generates a visualization of detection accuracy.

Usage:
    python3 test_naive_with_training_data.py
"""
import sys
import logging
from datetime import datetime
from pathlib import Path

import pandas as pd
import matplotlib
matplotlib.use('Agg')  # Use non-interactive backend
import matplotlib.pyplot as plt

from naive_config import NAIVE_CONFIG
from naive_tracker import NaiveTracker

# Suppress verbose logging during tests
logging.getLogger('naive_tracker').setLevel(logging.CRITICAL)
logging.getLogger().setLevel(logging.CRITICAL)


def test_scenario(scenario_name: str, scenario_df: pd.DataFrame, expected_label: int) -> dict:
    """
    Run a single scenario through the NaiveTracker and measure detection.

    Returns dict with metrics.
    """
    # Create fresh tracker for this scenario
    config = NAIVE_CONFIG.copy()
    config['log_file'] = f'/tmp/naive_test_{scenario_name}.log'
    config['state_file'] = f'/tmp/naive_test_{scenario_name}.json'

    tracker = NaiveTracker(config=config, log_only=True, verbose=False)

    # Sort by time
    scenario_df = scenario_df.sort_values('time')

    # Calculate scenario duration
    time_start = scenario_df['time'].min()
    time_end = scenario_df['time'].max()
    scenario_duration = time_end - time_start

    # Replay packets
    for _, row in scenario_df.iterrows():
        timestamp = datetime.fromtimestamp(row['time'])
        tracker.add_packet(
            client_ip=row['client'],
            server_ip=row['server'],
            packet_size=int(row['packet_size']),
            timestamp=timestamp
        )

    tracker.flush()

    # Get stats for all clients
    all_stats = tracker.get_all_stats()

    # Aggregate watch time across all clients
    total_watch_time = sum(s.get('watch_time_seconds', 0) for s in all_stats.values())

    # Calculate detection rate (cap at 100% for display)
    detection_rate = (total_watch_time / scenario_duration * 100) if scenario_duration > 0 else 0
    detection_rate = min(detection_rate, 100.0)  # Cap at 100%

    return {
        'scenario': scenario_name,
        'label': expected_label,
        'label_str': 'streaming' if expected_label == 1 else 'not streaming',
        'duration_sec': scenario_duration,
        'watch_time_sec': total_watch_time,
        'detection_rate': detection_rate,
        'num_packets': len(scenario_df),
        'num_clients': len(all_stats),
    }


def main():
    # Load HDF5 training data
    hdf_path = Path(__file__).parent / 'training' / 'raw.h5'

    if not hdf_path.exists():
        print(f"Error: HDF5 file not found at {hdf_path}")
        sys.exit(1)

    print(f"Loading training data from {hdf_path}...")
    df = pd.read_hdf(hdf_path, 'raw_packets')
    print(f"Loaded {len(df):,} packets")

    # Get unique scenarios
    scenarios = df['name'].unique()
    print(f"Found {len(scenarios)} scenarios: {list(scenarios)}")
    print()

    # Test each scenario
    results = []
    for scenario_name in sorted(scenarios):
        scenario_df = df[df['name'] == scenario_name].copy()
        expected_label = scenario_df['y'].iloc[0]

        print(f"Testing {scenario_name} (label={expected_label})...")
        result = test_scenario(scenario_name, scenario_df, expected_label)
        results.append(result)

        print(f"  Duration: {result['duration_sec']:.0f}s, "
              f"Detected: {result['watch_time_sec']:.1f}s, "
              f"Rate: {result['detection_rate']:.1f}%")

    print()

    # Create visualization
    create_plot(results)

    # Print summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)

    streaming_scenarios = [r for r in results if r['label'] == 1]
    non_streaming_scenarios = [r for r in results if r['label'] == 0]

    avg_streaming_rate = sum(r['detection_rate'] for r in streaming_scenarios) / len(streaming_scenarios)
    avg_non_streaming_rate = sum(r['detection_rate'] for r in non_streaming_scenarios) / len(non_streaming_scenarios)

    print(f"Streaming scenarios (y=1):     avg detection = {avg_streaming_rate:.1f}%")
    print(f"Non-streaming scenarios (y=0): avg detection = {avg_non_streaming_rate:.1f}%")
    print()

    # Recommendations
    if avg_streaming_rate < 50:
        print("Note: Low detection rate for streaming scenarios.")
        print("      This is expected due to ABR buffering (burst/silent pattern).")
        print("      Phase 3 buffer-credit accounting will improve this.")

    if avg_non_streaming_rate > 20:
        print("Warning: High false positive rate for non-streaming scenarios.")
        print("         Consider increasing rate_threshold or consecutive_windows.")


def create_plot(results: list):
    """Create a horizontal bar chart of detection results."""
    # Sort by label (streaming first), then by detection rate
    results = sorted(results, key=lambda r: (-r['label'], -r['detection_rate']))

    fig, ax = plt.subplots(figsize=(12, 8))

    # Data for plotting
    scenario_labels = [f"{r['scenario']}\n(y={r['label']})" for r in results]
    detection_rates = [r['detection_rate'] for r in results]
    durations = [r['duration_sec'] for r in results]
    labels = [r['label'] for r in results]

    # Colors: green for correct classification, orange for concerning
    colors = []
    for r in results:
        if r['label'] == 1:  # Should be streaming
            if r['detection_rate'] >= 30:
                colors.append('#2ecc71')  # Green - good
            else:
                colors.append('#f39c12')  # Orange - low detection
        else:  # Should NOT be streaming
            if r['detection_rate'] <= 20:
                colors.append('#2ecc71')  # Green - good (low false positives)
            else:
                colors.append('#e74c3c')  # Red - high false positives

    # Create horizontal bar chart
    y_pos = range(len(results))
    bars = ax.barh(y_pos, detection_rates, color=colors, edgecolor='black', linewidth=0.5)

    # Add percentage labels on bars
    for bar, rate, dur in zip(bars, detection_rates, durations):
        width = bar.get_width()
        label_x = width + 1 if width < 90 else width - 5
        ha = 'left' if width < 90 else 'right'
        color = 'black' if width < 90 else 'white'
        ax.text(label_x, bar.get_y() + bar.get_height()/2,
                f'{rate:.1f}% ({dur:.0f}s)',
                ha=ha, va='center', fontsize=9, color=color)

    # Customize plot
    ax.set_yticks(y_pos)
    ax.set_yticklabels(scenario_labels)
    ax.set_xlabel('Detection Rate (%)', fontsize=12)
    ax.set_title('Naive Tracker Detection Results per Training Scenario\n'
                 '(Green = correct, Orange = low detection, Red = false positives)',
                 fontsize=14)
    ax.set_xlim(0, 110)

    # Add vertical line at typical thresholds
    ax.axvline(x=50, color='gray', linestyle='--', alpha=0.5, label='50% threshold')
    ax.axvline(x=20, color='gray', linestyle=':', alpha=0.5, label='20% false positive limit')

    # Add legend
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor='#2ecc71', edgecolor='black', label='Good'),
        Patch(facecolor='#f39c12', edgecolor='black', label='Low detection (streaming)'),
        Patch(facecolor='#e74c3c', edgecolor='black', label='High false positive'),
    ]
    ax.legend(handles=legend_elements, loc='lower right')

    plt.tight_layout()

    # Save plot
    output_path = Path(__file__).parent / 'naive_validation_results.png'
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"\nPlot saved to: {output_path}")


if __name__ == "__main__":
    main()
