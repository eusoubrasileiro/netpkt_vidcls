# StreamGuard Test Suite

Comprehensive test suite with **57 tests** covering protocol detection, quota tracking, state persistence, and network filtering.

## Quick Start

```bash
cd src

# Run all tests (unit + integration)
make test

# Run unit tests only (fast, no pcap files needed)
make test-unit

# Run integration tests only (requires pcap files)
make test-integration

# TAP output for CI pipelines
make test-tap
```

## Test Architecture

```
test/
├── unit/                           # Criterion unit tests (C)
│   ├── test_protocol_detection.c   # Protocol classification
│   ├── test_lan_ip.c               # Subnet matching
│   ├── test_client_index.c         # Client index calculation
│   ├── test_flow_hash.c            # Flow hashing
│   └── test_state.c                # State persistence
├── integration/                    # Pcap-based integration tests
│   ├── run_tests.sh                # Bash test runner
│   ├── pcaps/                      # Test network captures
│   └── README.md                   # Pcap capture instructions
├── Makefile                        # Test build system
└── README.md                       # This file
```

### Unit Tests vs Integration Tests

| Type | Framework | Speed | What it tests |
|------|-----------|-------|---------------|
| **Unit** | Criterion (C) | Fast (~1s) | Individual functions in isolation |
| **Integration** | Bash + grep | Slower (~5s) | Full program with real network captures |

---

## Unit Tests (43 tests)

Unit tests use the [Criterion](https://github.com/Snaipe/Criterion) framework. Each test runs in an isolated process (crashes are caught).

### test_protocol_detection.c (12 tests)

Tests the core protocol classification logic that determines what traffic gets tracked.

#### Social Media Detection (`is_social_media_protocol()`)

| Test | Input | Expected | Why it matters |
|------|-------|----------|----------------|
| `instagram_is_social` | `NDPI_PROTOCOL_INSTAGRAM` | true | Instagram Reels/Stories should be tracked |
| `facebook_is_social` | `NDPI_PROTOCOL_FACEBOOK` | true | Facebook video should be tracked |
| `facebook_reel_is_social` | `NDPI_PROTOCOL_FACEBOOK_REEL_STORY` | true | FB Reels are video content |
| `youtube_is_not_social` | `NDPI_PROTOCOL_YOUTUBE` | false | YouTube is VIDEO category, not social |
| `http_is_not_social` | `NDPI_PROTOCOL_HTTP` | false | Generic HTTP is not social media |

#### Trackable Traffic Detection (`is_trackable_traffic()`)

| Test | Input | Expected | Why it matters |
|------|-------|----------|----------------|
| `video_category_is_trackable` | `NDPI_PROTOCOL_CATEGORY_VIDEO` | true | YouTube, Netflix, etc. |
| `streaming_category_is_trackable` | `NDPI_PROTOCOL_CATEGORY_STREAMING` | true | Twitch, live streams |
| `media_category_is_trackable` | `NDPI_PROTOCOL_CATEGORY_MEDIA` | true | General media content |
| `web_category_not_trackable` | `NDPI_PROTOCOL_CATEGORY_WEB` | false | Regular web browsing ignored |
| `social_trackable_in_default_mode` | Instagram + default mode | true | Default tracks social media |
| `social_not_trackable_in_video_only` | Instagram + `-V` flag | false | Video-only mode ignores social |
| `youtube_trackable_via_category` | YouTube + VIDEO category | true | YouTube tracked via category |

**Key insight**: Instagram/Facebook are in `SOCIAL_NETWORK` category (not VIDEO), so they require explicit protocol detection. YouTube is in `VIDEO` category and tracked automatically.

---

### test_lan_ip.c (9 tests)

Tests subnet matching to identify which IPs are local clients (to be tracked) vs external servers (ignored).

Default subnet: `192.168.0.0/24`

| Test | Input IP | Expected | Why it matters |
|------|----------|----------|----------------|
| `local_ip_in_range` | 192.168.0.100 | in LAN | Typical client IP |
| `local_ip_first` | 192.168.0.1 | in LAN | Router/gateway |
| `local_ip_last` | 192.168.0.254 | in LAN | Last usable address |
| `network_address` | 192.168.0.0 | in LAN | Network address |
| `broadcast_address` | 192.168.0.255 | in LAN | Broadcast address |
| `external_ip_google` | 8.8.8.8 | NOT in LAN | Google DNS |
| `external_ip_youtube` | 142.250.1.1 | NOT in LAN | YouTube server |
| `different_subnet` | 192.168.1.100 | NOT in LAN | Different /24 subnet |
| `private_10_net` | 10.0.0.1 | NOT in LAN | Different private range |

**Key insight**: Only traffic from LAN IPs gets quota tracked. External server IPs are ignored.

---

### test_client_index.c (8 tests)

Tests the client index calculation used to store per-client quota in the `clients[256]` array.

Formula: `index = last octet of IP address`

| Test | Input IP | Expected Index | Why it matters |
|------|----------|----------------|----------------|
| `extracts_last_octet_100` | 192.168.0.100 | 100 | Standard case |
| `index_zero` | 192.168.0.0 | 0 | Network address |
| `index_one` | 192.168.0.1 | 1 | Gateway |
| `index_255` | 192.168.0.255 | 255 | Broadcast |
| `index_25` | 192.168.0.25 | 25 | PC client from test pcap |
| `index_122` | 192.168.0.122 | 122 | Android client from test pcap |
| `different_subnet_same_last_octet` | 10.0.0.42 | 42 | Only last octet matters |
| `external_ip` | 142.250.1.138 | 138 | Works for any IP |

**Key insight**: Each client gets a slot in the 256-entry array based on their last octet. This limits tracking to /24 subnets.

---

### test_flow_hash.c (7 tests)

Tests the flow hashing used to track network connections bidirectionally.

#### Flow Hash Tests

| Test | Scenario | Expected | Why it matters |
|------|----------|----------|----------------|
| `same_flow_both_directions` | A→B vs B→A | Same hash | Request and response are same flow |
| `different_source_ports_different_hash` | Port 54321 vs 54322 | Different hash | Different connections distinguished |
| `tcp_vs_udp_different_hash` | TCP vs UDP same ports | Different hash | Protocol matters |
| `same_ips_different_ports_both_directions` | LAN↔LAN both ways | Same hash | Internal traffic works |

#### Flow Normalization Tests

| Test | Scenario | Expected | Why it matters |
|------|----------|----------|----------------|
| `smaller_ip_first` | Normalize 192.168→142.250 | Smaller IP first | Consistent ordering |
| `already_normalized` | Already ordered | No change | Idempotent |
| `same_ip_different_ports` | Same IPs | Sort by port | Handles edge case |

**Key insight**: Normalization ensures A→B and B→A hash to the same flow entry, so a YouTube video stream (client→server + server→client) is tracked as one connection.

---

### test_state.c (7 tests)

Tests JSON state persistence for quota tracking across restarts.

| Test | What it tests | Why it matters |
|------|---------------|----------------|
| `save_creates_file` | `save_state()` creates JSON file | State file appears on disk |
| `save_writes_json` | JSON contains IP, seconds, date | Correct format |
| `load_restores_clients` | `load_state()` restores client data | Quota survives restart |
| `roundtrip` | Save → clear → load preserves data | Full cycle works |
| `load_missing_file` | Missing file handled gracefully | First run doesn't crash |
| `load_invalid_json` | Corrupted JSON handled gracefully | Bad file doesn't crash |
| `save_no_path` | No crash when `-f` not specified | Default mode works |

**JSON format:**
```json
{
  "clients": [
    {
      "ip": "192.168.0.25",
      "streaming_seconds": 3600,
      "last_reset_date": "2026-01-09",
      "is_blocked": false
    }
  ]
}
```

---

## Integration Tests (14 tests)

Integration tests replay real network captures through StreamGuard and verify output patterns.

### Test Pcap Files

| File | Size | Content | Client IP |
|------|------|---------|-----------|
| `youtube_45sec.pcap` | ~6 MB | 45 seconds YouTube video | 192.168.0.25 |
| `instagram_45sec.pcap` | ~17 MB | 45 seconds Instagram scrolling | 192.168.0.122 |
| `multi_client.pcap` | ~95 MB | Multiple devices streaming | Various |
| `browsing_30sec.pcap` | ~40 MB | Web browsing (negative test) | 192.168.0.25 |

### Protocol Detection Tests

| Test | Pcap | Pattern | Validates |
|------|------|---------|-----------|
| **YouTube detection** | youtube_45sec.pcap | `"YouTube"` | nDPI detects YouTube via TLS SNI |
| **Instagram detection** | instagram_45sec.pcap | `"Instagram"` | Social media protocol detected |
| **Session start** | youtube_45sec.pcap | `"SESSION_START"` | Session begins on first packet |
| **Session end** | youtube_45sec.pcap | `"SESSION_END"` | Session ends at shutdown/timeout |
| **Multi-client tracking** | multi_client.pcap | `"SESSION_START"` | Multiple clients tracked separately |
| **Browsing not tracked** | browsing_30sec.pcap | `"STREAMING"` (NOT found) | Non-streaming traffic ignored |

### Mode Tests

| Test | Pcap | Flags | Pattern | Validates |
|------|------|-------|---------|-----------|
| **Video-only ignores Instagram** | instagram_45sec.pcap | `-V` | `"Instagram"` NOT found | `-V` flag filters social media |
| **Video-only tracks YouTube** | youtube_45sec.pcap | `-V` | `"YouTube"` found | `-V` still tracks VIDEO category |
| **Wrong subnet no tracking** | youtube_45sec.pcap | `-s 10.0.0.0` | `"STREAMING"` NOT found | Subnet filtering works |

### Quota Tests

| Test | Pcap | Flags | Validates |
|------|------|-------|-----------|
| **Quota limit exceeded** | youtube_45sec.pcap | `-q 30` | 42s > 30s quota (exceeded) |
| **Quota not exceeded** | youtube_45sec.pcap | `-q 60` | No "Would block" message |
| **YouTube quota (30-50s)** | youtube_45sec.pcap | - | Measured time in expected range |
| **Instagram quota (30-50s)** | instagram_45sec.pcap | - | Measured time in expected range |

### State Persistence Test

| Test | Pcap | Flags | Validates |
|------|------|-------|-----------|
| **State file created** | youtube_45sec.pcap | `-f /tmp/test.json` | JSON file created with `streaming_seconds` |

---

## Adding New Tests

### Adding a Unit Test

1. Add test function to appropriate file in `test/unit/`:

```c
Test(suite_name, test_name) {
    // Setup
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.100", &addr);

    // Act
    int result = is_lan_ip(addr.s_addr);

    // Assert
    cr_assert_eq(result, 1, "192.168.0.100 should be in LAN");
}
```

2. Run: `make test-unit`

### Adding an Integration Test

1. Add to `test/integration/run_tests.sh`:

```bash
# Pattern match test
test "Test name"  pcap_file.pcap  "pattern"  "flags"  "expect"

# Quota range test
test_quota "Test name"  pcap_file.pcap  min_seconds  max_seconds
```

2. Run: `make test-integration`

### Creating New Test Pcaps

See `test/integration/README.md` for capture instructions. Key points:
- Use `-s 1024` for header-only captures (small files)
- Use `timeout` to auto-stop capture
- 45-60 seconds of activity is sufficient

---

## Test Dependencies

**Unit tests require:**
- `libcriterion-dev` - Install: `sudo apt install libcriterion-dev`

**Integration tests require:**
- Test pcap files in `test/integration/pcaps/`
- Compiled `streamguard` binary

---

## Troubleshooting

### "SKIP (pcap not found)"
Pcap files are not included in git. Capture them manually (see `test/integration/README.md`).

### Unit test crashes
Criterion catches crashes and reports them. Check the test output for the failing assertion.

### Quota tests fail with wrong values
Pcap timing depends on actual network activity during capture. Re-capture if needed.
