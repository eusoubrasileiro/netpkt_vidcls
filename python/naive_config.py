"""
Configuration for naive throughput-based video streaming detector.

Uses a buffer credit model: bursts accumulate "video seconds" which drain
at playback rate. Streaming state persists while buffer > 0.
"""
import pathlib

NAIVE_CONFIG = {
    # Network settings (inherit from main config)
    'lan_subnet': '192.168.0.0',
    'lan_subnet_mask': 24,

    # Window settings
    'window_size': 5,             # seconds per window

    # Buffer credit model parameters
    'video_bitrate': 560_000,     # bytes/sec (~4.5 Mbps for 720p reference)
    'drain_rate': 0.6,            # playback consumption multiplier (tuned for balance)
    'entry_threshold': 10.0,      # seconds of buffer to trigger STREAMING_START
    'exit_threshold': 0.0,        # seconds of buffer to trigger STREAMING_STOP

    # Quota settings
    'daily_quota_seconds': 3600,  # 1 hour default per client
    'reset_hour': 0,              # reset quotas at midnight

    # Enforcement (Phase 2 - disabled for now)
    'block_timeout': 7200,        # 2h timeout for nftables blocks
    'cooldown_seconds': 30,       # grace period before first block
    'progressive_blocking': True, # block one server at a time

    # State persistence and logging
    'state_file': pathlib.Path(__file__).parent / 'naive_state.json',
    'log_file': pathlib.Path(__file__).parent / 'naive_tracker.log',
}
