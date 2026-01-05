#!/usr/bin/env python3
"""
Parameter search for NaiveTracker buffer credit model using sklearn.

Uses GridSearchCV/RandomizedSearchCV to find optimal parameters that
maximize streaming detection while minimizing false positives.
"""
import sys
import logging
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Tuple

import numpy as np
import pandas as pd
from sklearn.base import BaseEstimator, ClassifierMixin
from sklearn.model_selection import ParameterGrid
from joblib import Parallel, delayed

from naive_config import NAIVE_CONFIG
from naive_tracker import NaiveTracker

# Suppress ALL logging during search
logging.disable(logging.CRITICAL)


class NaiveTrackerEstimator(BaseEstimator, ClassifierMixin):
    """
    Sklearn-compatible wrapper for NaiveTracker.

    Parameters are the buffer credit model params we want to tune.
    """

    def __init__(self, video_bitrate=560_000, drain_rate=0.6,
                 entry_threshold=10.0, exit_threshold=0.0, window_size=5):
        self.video_bitrate = video_bitrate
        self.drain_rate = drain_rate
        self.entry_threshold = entry_threshold
        self.exit_threshold = exit_threshold
        self.window_size = window_size

    def fit(self, X, y=None):
        """No fitting needed - this is a rule-based model."""
        return self

    def predict(self, X: List[pd.DataFrame]) -> np.ndarray:
        """
        Run tracker on each scenario and return detection rates.

        X: List of (scenario_df, expected_label) tuples
        Returns: Array of detection rates
        """
        predictions = []
        for scenario_df, label in X:
            detection_rate = self._run_scenario(scenario_df)
            predictions.append(detection_rate)
        return np.array(predictions)

    def score(self, X: List[Tuple[pd.DataFrame, int]], y=None) -> float:
        """
        Custom scoring: maximize streaming detection, minimize false positives.

        Score = avg_streaming_detection - 2 * avg_false_positive

        The multiplier on FP penalizes false positives more heavily.
        """
        streaming_rates = []
        non_streaming_rates = []

        for scenario_df, label in X:
            detection_rate = self._run_scenario(scenario_df)
            if label == 1:
                streaming_rates.append(detection_rate)
            else:
                non_streaming_rates.append(detection_rate)

        avg_streaming = np.mean(streaming_rates) if streaming_rates else 0
        avg_fp = np.mean(non_streaming_rates) if non_streaming_rates else 0

        # Score: reward high detection, penalize false positives
        # Adjust weights as needed
        score = avg_streaming - 2.0 * avg_fp
        return score

    def _run_scenario(self, scenario_df: pd.DataFrame) -> float:
        """Run a single scenario through the tracker and return detection rate."""
        config = NAIVE_CONFIG.copy()
        config['video_bitrate'] = self.video_bitrate
        config['drain_rate'] = self.drain_rate
        config['entry_threshold'] = self.entry_threshold
        config['exit_threshold'] = self.exit_threshold
        config['window_size'] = self.window_size
        config['log_file'] = '/tmp/param_search.log'
        config['state_file'] = '/tmp/param_search.json'

        tracker = NaiveTracker(config=config, log_only=True, verbose=False, silent=True)

        scenario_df = scenario_df.sort_values('time')
        time_start = scenario_df['time'].min()
        time_end = scenario_df['time'].max()
        duration = time_end - time_start

        for _, row in scenario_df.iterrows():
            timestamp = datetime.fromtimestamp(row['time'])
            tracker.add_packet(
                client_ip=row['client'],
                server_ip=row['server'],
                packet_size=int(row['packet_size']),
                timestamp=timestamp
            )

        tracker.flush()

        all_stats = tracker.get_all_stats()
        total_watch_time = sum(s.get('watch_time_seconds', 0) for s in all_stats.values())

        detection_rate = (total_watch_time / duration * 100) if duration > 0 else 0
        return min(detection_rate, 100.0)


def load_training_data() -> List[Tuple[pd.DataFrame, int, str]]:
    """Load all scenarios from HDF5 file."""
    hdf_path = Path(__file__).parent / 'training' / 'raw.h5'

    if not hdf_path.exists():
        print(f"Error: HDF5 file not found at {hdf_path}")
        sys.exit(1)

    df = pd.read_hdf(hdf_path, 'raw_packets')

    scenarios = []
    for scenario_name in df['name'].unique():
        scenario_df = df[df['name'] == scenario_name].copy()
        label = scenario_df['y'].iloc[0]
        scenarios.append((scenario_df, label, scenario_name))

    return scenarios


def evaluate_params(params, X, scenario_names):
    """Evaluate a single parameter configuration (for parallel execution)."""
    estimator = NaiveTrackerEstimator(**params)

    streaming_rates = []
    non_streaming_rates = []
    scenario_results = {}

    for (scenario_df, label), name in zip(X, scenario_names):
        rate = estimator._run_scenario(scenario_df)
        scenario_results[name] = rate
        if label == 1:
            streaming_rates.append(rate)
        else:
            non_streaming_rates.append(rate)

    avg_streaming = np.mean(streaming_rates)
    avg_fp = np.mean(non_streaming_rates)
    score = avg_streaming - 2.0 * avg_fp

    return {
        **params,
        'score': score,
        'avg_streaming': avg_streaming,
        'avg_fp': avg_fp,
        **scenario_results
    }


def grid_search(n_jobs=-1):
    """Run grid search over parameter space using parallel processing."""
    print("Loading training data...")
    scenarios = load_training_data()
    print(f"Loaded {len(scenarios)} scenarios")

    # Prepare data as list of (df, label) tuples
    X = [(s[0], s[1]) for s in scenarios]
    scenario_names = [s[2] for s in scenarios]

    # Parameter grid
    param_grid = {
        'video_bitrate': [400_000, 560_000, 750_000],  # 3.2, 4.5, 6 Mbps
        'drain_rate': [0.3, 0.4, 0.5, 0.6, 0.7, 0.8],
        'entry_threshold': [5.0, 10.0, 15.0, 20.0],
        'exit_threshold': [0.0],  # Keep at 0 for now
        'window_size': [5],  # Keep at 5 for now
    }

    param_list = list(ParameterGrid(param_grid))
    print(f"\nParameter grid: {len(param_list)} combinations")
    print(f"Running with {n_jobs} parallel jobs (-1 = all cores)...")

    # Parallel execution
    results = Parallel(n_jobs=n_jobs, verbose=10)(
        delayed(evaluate_params)(params, X, scenario_names)
        for params in param_list
    )

    # Convert to DataFrame and sort
    results_df = pd.DataFrame(results)
    results_df = results_df.sort_values('score', ascending=False)

    # Save results
    results_df.to_csv('param_search_results.csv', index=False)
    print("\nResults saved to param_search_results.csv")

    # Get best result
    best_row = results_df.iloc[0]
    best_params = {
        'video_bitrate': int(best_row['video_bitrate']),
        'drain_rate': best_row['drain_rate'],
        'entry_threshold': best_row['entry_threshold'],
        'exit_threshold': best_row['exit_threshold'],
        'window_size': int(best_row['window_size']),
    }

    # Print top 10 results
    print("\n" + "=" * 80)
    print("TOP 10 CONFIGURATIONS")
    print("=" * 80)

    for idx, (_, row) in enumerate(results_df.head(10).iterrows()):
        print(f"\n#{idx+1}: score={row['score']:.1f}")
        print(f"  video_bitrate={int(row['video_bitrate'])}, drain_rate={row['drain_rate']}, "
              f"entry_threshold={row['entry_threshold']}")
        print(f"  Streaming: {row['avg_streaming']:.1f}% | FP: {row['avg_fp']:.1f}%")

    # Print best configuration details
    print("\n" + "=" * 80)
    print("BEST CONFIGURATION")
    print("=" * 80)
    print(f"\nParameters:")
    for k, v in best_params.items():
        print(f"  {k}: {v}")

    print(f"\nResults:")
    print(f"  Score: {best_row['score']:.1f}")
    print(f"  Avg Streaming Detection: {best_row['avg_streaming']:.1f}%")
    print(f"  Avg False Positive: {best_row['avg_fp']:.1f}%")

    # Per-scenario breakdown
    scenario_cols = [c for c in results_df.columns if 'packets' in c]
    print(f"\nPer-scenario breakdown:")
    for name in scenario_cols:
        label = "streaming" if any(s[2] == name and s[1] == 1 for s in scenarios) else "NOT streaming"
        print(f"  {name}: {best_row[name]:.1f}% ({label})")

    return best_params, results_df


def random_search(n_iter=50):
    """Run randomized search with more parameter values."""
    print("Loading training data...")
    scenarios = load_training_data()
    print(f"Loaded {len(scenarios)} scenarios")

    X = [(s[0], s[1]) for s in scenarios]
    scenario_names = [s[2] for s in scenarios]

    # Wider parameter ranges for random search
    param_ranges = {
        'video_bitrate': (300_000, 900_000),  # 2.4 - 7.2 Mbps
        'drain_rate': (0.2, 0.9),
        'entry_threshold': (3.0, 25.0),
    }

    print(f"\nRunning {n_iter} random configurations...")

    best_score = -np.inf
    best_params = None
    best_results = None
    results = []

    np.random.seed(42)

    for i in range(n_iter):
        params = {
            'video_bitrate': int(np.random.uniform(*param_ranges['video_bitrate'])),
            'drain_rate': round(np.random.uniform(*param_ranges['drain_rate']), 2),
            'entry_threshold': round(np.random.uniform(*param_ranges['entry_threshold']), 1),
            'exit_threshold': 0.0,
            'window_size': 5,
        }

        estimator = NaiveTrackerEstimator(**params)
        score = estimator.score(X)

        streaming_rates = []
        non_streaming_rates = []
        scenario_results = {}

        for (scenario_df, label), name in zip(X, scenario_names):
            rate = estimator._run_scenario(scenario_df)
            scenario_results[name] = rate
            if label == 1:
                streaming_rates.append(rate)
            else:
                non_streaming_rates.append(rate)

        avg_streaming = np.mean(streaming_rates)
        avg_fp = np.mean(non_streaming_rates)

        results.append({
            **params,
            'score': score,
            'avg_streaming': avg_streaming,
            'avg_fp': avg_fp,
        })

        if score > best_score:
            best_score = score
            best_params = params
            best_results = {
                'avg_streaming': avg_streaming,
                'avg_fp': avg_fp,
                'scenarios': scenario_results
            }

        print(f"\r  [{i+1}/{n_iter}] score={score:.1f} stream={avg_streaming:.1f}% "
              f"fp={avg_fp:.1f}% (best={best_score:.1f})", end='')

    print("\n")

    results_df = pd.DataFrame(results).sort_values('score', ascending=False)
    results_df.to_csv('random_search_results.csv', index=False)

    print("\n" + "=" * 80)
    print("BEST CONFIGURATION (Random Search)")
    print("=" * 80)
    print(f"\nParameters:")
    for k, v in best_params.items():
        print(f"  {k}: {v}")
    print(f"\nResults:")
    print(f"  Score: {best_score:.1f}")
    print(f"  Avg Streaming Detection: {best_results['avg_streaming']:.1f}%")
    print(f"  Avg False Positive: {best_results['avg_fp']:.1f}%")

    return best_params, results_df


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description='Parameter search for NaiveTracker')
    parser.add_argument('--method', choices=['grid', 'random'], default='grid',
                        help='Search method (default: grid)')
    parser.add_argument('--n-iter', type=int, default=50,
                        help='Number of iterations for random search (default: 50)')
    args = parser.parse_args()

    if args.method == 'grid':
        grid_search()
    else:
        random_search(n_iter=args.n_iter)
