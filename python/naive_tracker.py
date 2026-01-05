"""
Naive throughput-based video streaming detector.

Tracks bytes per (client_ip, server_ip) pair and detects streaming
based on sustained high throughput over configurable time windows.

Phase 1: Logging only - validates approach before enforcement.
"""
import json
import logging
from datetime import datetime, date
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, Set, Optional

from naive_config import NAIVE_CONFIG


@dataclass
class FlowState:
    """State for a single (client, server) flow."""
    bytes_total: int = 0
    bytes_in_window: int = 0
    buffer_credit: float = 0.0  # Buffer in seconds of video content
    is_streaming: bool = False
    last_packet_time: Optional[datetime] = None


@dataclass
class ClientState:
    """State for a single client IP."""
    flows: Dict[str, FlowState] = field(default_factory=dict)  # server_ip -> FlowState
    watch_time_seconds: float = 0.0
    streaming_start: Optional[datetime] = None
    last_reset_date: Optional[date] = None

    def get_or_create_flow(self, server_ip: str) -> FlowState:
        if server_ip not in self.flows:
            self.flows[server_ip] = FlowState()
        return self.flows[server_ip]


class NaiveTracker:
    """
    Tracks network throughput per (client, server) pair and detects video streaming.

    Detection algorithm:
    1. Accumulate bytes per flow in 5-second windows
    2. Calculate rate = bytes_in_window / window_size
    3. If rate > threshold for consecutive_windows times -> mark as STREAMING
    4. While streaming, accumulate watch time
    5. Log all state changes
    """

    def __init__(self, config: dict = None, log_only: bool = True, verbose: bool = False, silent: bool = False):
        self.config = config or NAIVE_CONFIG
        self.log_only = log_only
        self.verbose = verbose
        self.silent = silent

        # State per client IP
        self.clients: Dict[str, ClientState] = defaultdict(ClientState)

        # Current window tracking
        self.window_start: Optional[datetime] = None
        self.window_end: Optional[datetime] = None  # Track last packet time in window
        self.window_bytes: Dict[str, Dict[str, int]] = defaultdict(lambda: defaultdict(int))

        # Setup logging (use null logger if silent mode)
        if silent:
            self.logger = logging.getLogger('null')
            self.logger.addHandler(logging.NullHandler())
            self.logger.setLevel(logging.CRITICAL + 1)  # Disable all logging
        else:
            self._setup_logging()
            self.logger.info(f"NaiveTracker initialized (log_only={log_only}, verbose={verbose})")
            self.logger.info(f"Buffer model: video_bitrate={self.config['video_bitrate']} B/s, "
                            f"drain_rate={self.config['drain_rate']}, "
                            f"entry={self.config['entry_threshold']}s, "
                            f"window={self.config['window_size']}s")

    def _setup_logging(self):
        """Configure file and console logging."""
        self.logger = logging.getLogger('naive_tracker')
        self.logger.setLevel(logging.DEBUG if self.verbose else logging.INFO)

        # File handler
        fh = logging.FileHandler(self.config['log_file'])
        fh.setLevel(logging.DEBUG)
        fh.setFormatter(logging.Formatter(
            '%(asctime)s | %(levelname)-5s | %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        ))
        self.logger.addHandler(fh)

        # Console handler (less verbose)
        ch = logging.StreamHandler()
        ch.setLevel(logging.INFO)
        ch.setFormatter(logging.Formatter(
            '%(asctime)s | %(message)s',
            datefmt='%H:%M:%S'
        ))
        self.logger.addHandler(ch)

    def add_packet(self, client_ip: str, server_ip: str, packet_size: int, timestamp: datetime):
        """
        Process a single packet and accumulate bytes.

        Args:
            client_ip: LAN client IP address
            server_ip: WAN server IP address
            packet_size: Size of packet in bytes
            timestamp: Packet timestamp
        """
        # Initialize window if needed
        if self.window_start is None:
            self.window_start = timestamp

        # Track last packet time for accurate duration calculation
        self.window_end = timestamp

        # Check if we've completed a window
        window_elapsed = (timestamp - self.window_start).total_seconds()
        if window_elapsed >= self.config['window_size']:
            self._process_window()
            self.window_start = timestamp

        # Accumulate bytes for this flow
        self.window_bytes[client_ip][server_ip] += packet_size

    def _process_window(self):
        """Process completed time window and update streaming state using buffer credit model."""
        if not self.window_bytes:
            return

        window_size = self.config['window_size']
        video_bitrate = self.config['video_bitrate']
        drain_rate = self.config['drain_rate']
        entry_threshold = self.config['entry_threshold']
        exit_threshold = self.config['exit_threshold']

        # Use packet timestamp for accurate time tracking
        now = self.window_end or datetime.now()

        # Check for daily quota reset (use packet date for testing compatibility)
        today = now.date() if isinstance(now, datetime) else date.today()

        for client_ip, server_bytes in self.window_bytes.items():
            client = self.clients[client_ip]

            # Reset daily quota if new day
            if client.last_reset_date != today:
                if client.watch_time_seconds > 0:
                    self.logger.info(
                        f"DAILY_RESET | client={client_ip} | "
                        f"previous_watch_time={client.watch_time_seconds:.1f}s"
                    )
                client.watch_time_seconds = 0.0
                client.last_reset_date = today

            # Track if any flow is streaming
            any_streaming = False

            for server_ip, bytes_in_window in server_bytes.items():
                flow = client.get_or_create_flow(server_ip)
                flow.bytes_total += bytes_in_window
                flow.bytes_in_window = bytes_in_window

                # Buffer credit model:
                # - Convert downloaded bytes to video seconds
                # - Drain at constant rate (playback consumption)
                seconds_downloaded = bytes_in_window / video_bitrate
                seconds_drained = drain_rate * window_size

                # Update buffer credit
                flow.buffer_credit += seconds_downloaded
                flow.buffer_credit -= seconds_drained
                flow.buffer_credit = max(flow.buffer_credit, 0)

                was_streaming = flow.is_streaming

                # State transitions based on buffer credit
                if flow.buffer_credit >= entry_threshold and not flow.is_streaming:
                    flow.is_streaming = True
                    self.logger.info(
                        f"STREAMING_START | client={client_ip} | server={server_ip} | "
                        f"buffer={flow.buffer_credit:.1f}s | +{seconds_downloaded:.1f}s downloaded"
                    )
                elif flow.buffer_credit <= exit_threshold and flow.is_streaming:
                    flow.is_streaming = False
                    self.logger.info(
                        f"STREAMING_STOP | client={client_ip} | server={server_ip} | "
                        f"buffer={flow.buffer_credit:.1f}s | total_bytes={flow.bytes_total}"
                    )

                if flow.is_streaming:
                    any_streaming = True

                # Verbose per-flow logging
                if self.verbose:
                    status = "STREAM" if flow.is_streaming else "idle"
                    self.logger.debug(
                        f"FLOW | {client_ip} -> {server_ip} | "
                        f"{bytes_in_window:>8} B | +{seconds_downloaded:>5.1f}s | "
                        f"buf={flow.buffer_credit:>5.1f}s | {status}"
                    )

            # Update client watch time
            was_watching = client.streaming_start is not None

            if any_streaming:
                if not was_watching:
                    client.streaming_start = now
                    self.logger.info(f"WATCH_START | client={client_ip}")
            else:
                if was_watching:
                    # Calculate watch duration
                    duration = (now - client.streaming_start).total_seconds()
                    client.watch_time_seconds += duration
                    client.streaming_start = None
                    self.logger.info(
                        f"WATCH_STOP | client={client_ip} | "
                        f"session={duration:.1f}s | total={client.watch_time_seconds:.1f}s"
                    )

            # Log client summary
            quota = self.config['daily_quota_seconds']
            current_watch = client.watch_time_seconds
            if client.streaming_start:
                current_watch += (now - client.streaming_start).total_seconds()

            quota_pct = (current_watch / quota) * 100 if quota > 0 else 0
            status = "STREAMING" if any_streaming else "idle"

            self.logger.info(
                f"CLIENT | {client_ip:<15} | {status:<9} | "
                f"watch_time={current_watch:>6.1f}s | quota={quota_pct:>5.1f}%"
            )

            # Check quota exceeded (Phase 2 will enforce)
            if current_watch >= quota and self.log_only:
                self.logger.warning(
                    f"QUOTA_EXCEEDED | client={client_ip} | "
                    f"watch_time={current_watch:.1f}s | quota={quota}s | "
                    f"(enforcement disabled - log_only mode)"
                )

        # Clear window bytes for next window
        self.window_bytes.clear()

    def flush(self):
        """Process any remaining data in the current window."""
        if self.window_bytes:
            self._process_window()

    def get_client_stats(self, client_ip: str) -> dict:
        """Get statistics for a specific client."""
        client = self.clients.get(client_ip)
        if not client:
            return {}

        # Use last packet timestamp for historical data compatibility
        now = self.window_end or datetime.now()
        current_watch = client.watch_time_seconds
        if client.streaming_start:
            current_watch += (now - client.streaming_start).total_seconds()

        active_flows = [
            (server_ip, flow) for server_ip, flow in client.flows.items()
            if flow.is_streaming
        ]

        return {
            'client_ip': client_ip,
            'watch_time_seconds': current_watch,
            'quota_seconds': self.config['daily_quota_seconds'],
            'quota_percent': (current_watch / self.config['daily_quota_seconds']) * 100,
            'is_streaming': len(active_flows) > 0,
            'active_servers': [ip for ip, _ in active_flows],
            'total_flows': len(client.flows),
        }

    def get_all_stats(self) -> dict:
        """Get statistics for all clients."""
        return {
            client_ip: self.get_client_stats(client_ip)
            for client_ip in self.clients
        }

    def save_state(self):
        """Persist state to JSON file for recovery."""
        state = {
            'timestamp': datetime.now().isoformat(),
            'clients': {}
        }

        for client_ip, client in self.clients.items():
            state['clients'][client_ip] = {
                'watch_time_seconds': client.watch_time_seconds,
                'last_reset_date': client.last_reset_date.isoformat() if client.last_reset_date else None,
                'flows': {
                    server_ip: {
                        'bytes_total': flow.bytes_total,
                        'is_streaming': flow.is_streaming,
                    }
                    for server_ip, flow in client.flows.items()
                }
            }

        with open(self.config['state_file'], 'w') as f:
            json.dump(state, f, indent=2)

        self.logger.info(f"State saved to {self.config['state_file']}")

    def load_state(self):
        """Load state from JSON file."""
        try:
            with open(self.config['state_file'], 'r') as f:
                state = json.load(f)

            for client_ip, client_data in state.get('clients', {}).items():
                client = self.clients[client_ip]
                client.watch_time_seconds = client_data.get('watch_time_seconds', 0.0)

                reset_date = client_data.get('last_reset_date')
                if reset_date:
                    client.last_reset_date = date.fromisoformat(reset_date)

                for server_ip, flow_data in client_data.get('flows', {}).items():
                    flow = client.get_or_create_flow(server_ip)
                    flow.bytes_total = flow_data.get('bytes_total', 0)

            self.logger.info(f"State loaded from {self.config['state_file']}")

        except FileNotFoundError:
            self.logger.info("No saved state found, starting fresh")
        except json.JSONDecodeError as e:
            self.logger.error(f"Failed to load state: {e}")
