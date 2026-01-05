"""
Configuration for naive throughput-based video streaming detector.

Phase 1: Logging only (no enforcement)
"""
import pathlib

NAIVE_CONFIG = {
    # Network settings (inherit from main config)
    'lan_subnet': '192.168.0.0',
    'lan_subnet_mask': 24,

    # Detection thresholds
    'window_size': 5,             # seconds per window
    'rate_threshold': 50_000,     # bytes/sec (400 kbps) to consider streaming
                                  # Based on training data: streaming bursts ~90-400 KB/s
    'consecutive_windows': 3,     # must sustain for 15s (3 windows) to count as streaming

    # Quota settings
    'daily_quota_seconds': 3600,  # 1 hour default per client
    'reset_hour': 0,              # reset quotas at midnight

    # Enforcement (Phase 2 - disabled for now)
    'block_timeout': 7200,        # 2h timeout for nftables blocks
    'cooldown_seconds': 30,       # grace period before first block
    'progressive_blocking': True, # block one server at a time

    # Phase 3 refinements (DISABLED - experimental, increases false positives)
    'use_ewma': False,            # EWMA smoothing (tested, increases FP from 2.5% to 16%)
    'ewma_alpha': 0.3,            # EWMA weight (higher = more responsive)
    'use_buffer_credit': False,   # buffer accounting (tested, no measurable improvement)
    'max_buffer_seconds': 30,     # max buffer credit
    'estimated_video_bitrate': 500_000,  # bytes/sec for buffer calculation

    # State persistence and logging
    'state_file': pathlib.Path(__file__).parent / 'naive_state.json',
    'log_file': pathlib.Path(__file__).parent / 'naive_tracker.log',
}
