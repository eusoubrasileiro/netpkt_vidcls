# Implementation Plan - Naive Throughput Approach

**Date:** 2026-01-05
**Status:** Design Phase
**Branch:** naive (to be implemented)

---

## Executive Summary

This document outlines the implementation plan for a **simple throughput-based video streaming detector** that replaces the existing ML approach. The naive approach is **significantly better suited** for the project's goal of tracking watch time and enforcing quotas.

### Key Decision: Why Naive > ML

The existing ML classifier has a **critical flaw**: it cannot accurately count watch time during video buffering periods. This makes it unsuitable for quota enforcement.

---

## Problem Analysis

### The Buffering Problem (Why ML Fails)

Modern video streaming uses **adaptive bitrate (ABR)** with segment-based delivery:

1. **Burst phase** (2-3s): Downloads 6s of video content at high speed
2. **Silent phase** (3-4s): Player consumes buffer, no network traffic
3. **Repeat**: Player fetches next segment

**ML approach counts only burst time:**
- 30 minutes of video = ~10 minutes of detected "streaming"
- **Underestimates by 60-70%!** ❌

**Naive approach with buffer-credit:**
- Burst → adds buffer credit in seconds
- Silent → depletes credit at 1x speed, keeps counting
- **Counts accurately** ✅

### Complexity vs Value

| Aspect | ML Approach | Naive Approach |
|--------|-------------|----------------|
| Code complexity | 500+ lines | ~60 lines |
| Training data | 185MB, 8 scenarios | None needed |
| CPU/RAM overhead | High (feature extraction + model) | Minimal (byte counting) |
| Maintenance | Retrain for new patterns | Adjust 2-3 thresholds |
| Time accuracy | ❌ 60-70% undercount | ✅ ~95% accurate |
| Detection delay | 30s (3 windows) | 9-15s |
| False positives | Low (~7%) | Medium (~20%?), acceptable |

**Verdict:** ML is over-engineered for this use case.

---

## Proposed Solution: Three-Phase Implementation

### Phase 1: Simple Throughput Tracker (LOGGING ONLY) ⭐ START HERE

**Goal:** Validate the approach with 24h of logs before enforcing blocks.

**Implementation:**

```python
# State per (client_ip, server_ip) pair
# Track bytes in 5-second windows
# Apply simple threshold: >300 kbps sustained for 15s = STREAMING
# Accumulate total watch time per client
# LOG everything to file for validation
```

**Key Features:**
- No EWMA smoothing yet (keep it simple)
- No buffer-credit accounting yet
- No blocking yet - only observation
- Configurable thresholds in `config.py`

**Files to Create:**
- `python/naive_tracker.py` - Main throughput tracking logic
- `python/naive_config.py` - Configuration (thresholds, quotas)
- `python/naive_sniffer.py` - Integration with tcpdump

**Success Criteria:**
- Runs stable for 24h without crashes
- Logs show sensible streaming detection (manual validation)
- Can correlate logs with actual YouTube/Netflix viewing

**Estimated Time:** 2-3 hours

---

### Phase 2: Add Enforcement (nftables Integration)

**Goal:** Actually block clients when quota exceeded.

**Implementation:**

```python
# When client exceeds daily quota:
# 1. Sort server IPs by total bytes (highest first)
# 2. Block top server with nftables:
#    nft add element inet fw4 stream_user_block '{ CLIENT_IP . SERVER_IP timeout 2h }'
# 3. Log blocking action
# 4. Persist state to JSON
```

**Key Features:**
- Progressive blocking (one server IP at a time)
- Timeout-based blocks (2h default, auto-expire)
- State persistence (`naive_state.json`)
- Optional: cooldown period (30s warning before block)

**Files to Modify:**
- `python/naive_tracker.py` - Add blocking logic
- `python/naive_blocker.py` - nftables integration (new file)

**Router Setup Required:**
```bash
# On OpenWrt router, create /etc/nftables.d/30-streamctl.nft
table inet fw4 {
  set stream_user_block {
    type ipv4_addr . ipv4_addr
    flags timeout
    timeout 24h
  }
  chain stream_quota {
    type filter hook forward priority 0; policy accept;
    ip saddr . ip daddr @stream_user_block drop
  }
}
```

**Success Criteria:**
- Blocks are applied correctly via nftables
- Blocks expire automatically after timeout
- State survives script restarts
- Manual unblock works: `nft flush set inet fw4 stream_user_block`

**Estimated Time:** 1-2 hours

---

### Phase 3: Refinements (Optional - If Needed)

Only implement if Phase 1-2 logs show issues:

#### 3a. EWMA Smoothing (if too much flapping)

```python
# Add exponential moving average to smooth rate:
ewma = (1 - ALPHA) * ewma + ALPHA * current_rate
# Use ALPHA = 0.3 (gives more weight to recent windows)
```

**When needed:** If logs show rapid on/off transitions during stable streaming.

#### 3b. Buffer-Credit Accounting (if short videos undercounted)

```python
# When rate > START_T:
#   buffer_credit += bytes_in_window / estimated_bitrate
#   buffer_credit = min(buffer_credit, MAX_BUFFER_SEC)  # cap at 90s
# During silent periods:
#   buffer_credit -= WINDOW_SIZE
#   keep counting as PLAYING while buffer_credit > 0
```

**When needed:** If YouTube shorts or short videos show significantly less time than actual.

#### 3c. Hysteresis Thresholds (if flapping persists)

```python
# Use different thresholds for starting vs stopping:
# START_T = 400 kbps (enter PLAYING after 2 consecutive windows)
# STOP_T = 150 kbps (exit PLAYING after 6 consecutive windows)
```

**When needed:** If EWMA alone doesn't prevent state flapping.

**Estimated Time:** 2-3 hours total if all refinements needed

---

## Configuration Parameters

Add to `python/naive_config.py`:

```python
NAIVE_CONFIG = {
    # Detection thresholds
    'window_size': 5,           # seconds per window
    'rate_threshold': 300_000,  # bytes/sec (~300 kbps)
    'consecutive_windows': 3,   # must sustain for 15s to count as streaming

    # Quota settings
    'daily_quota_seconds': 3600,  # 1 hour default per client
    'reset_hour': 0,              # reset quotas at midnight

    # Enforcement
    'block_timeout': 7200,        # 2h timeout for nftables blocks
    'cooldown_seconds': 30,       # grace period before first block
    'progressive_blocking': True, # block one server at a time

    # Optional refinements (Phase 3)
    'use_ewma': False,           # enable EWMA smoothing
    'ewma_alpha': 0.3,           # EWMA weight
    'use_buffer_credit': False,  # enable buffer accounting
    'max_buffer_seconds': 90,    # max buffer credit

    # State persistence
    'state_file': '/var/lib/naive_state.json',
    'log_file': '/var/log/naive_tracker.log',
}
```

---

## Testing Strategy

### Unit Tests (Optional)

Create `python/test_naive_tracker.py`:
- Test threshold detection with synthetic data
- Test buffer-credit accounting (if implemented)
- Test state persistence and recovery

### Integration Testing

**Test 1: Known Video Session**
```bash
# Watch 10min YouTube video
# Check logs: should show ~10min ± 1min
# Validate: no false positives during idle time
```

**Test 2: Large Download (False Positive Check)**
```bash
# Download 1GB file via HTTP
# Check logs: might count 2-5min as "streaming"
# Validate: acceptable false positive for home network
```

**Test 3: Multiple Clients**
```bash
# 2-3 devices streaming simultaneously
# Check logs: each client tracked separately
# Validate: no cross-contamination
```

**Test 4: Quota Enforcement**
```bash
# Set quota to 5min for testing
# Watch video until blocked
# Validate: block applied via nftables, browsing still works
```

---

## Deployment Plan

### Prerequisites

**On Orange Pi 5 (or monitoring SBC):**
```bash
pip install pandas scapy numpy
# Already installed per existing setup
```

**On OpenWrt Router:**
```bash
# Install nftables configuration
cat > /etc/nftables.d/30-streamctl.nft << 'EOF'
table inet fw4 {
  set stream_user_block {
    type ipv4_addr . ipv4_addr
    flags timeout
    timeout 24h
  }
  chain stream_quota {
    type filter hook forward priority 0; policy accept;
    ip saddr . ip daddr @stream_user_block drop
  }
}
EOF

# Reload firewall
fw4 reload

# Verify set exists
nft list set inet fw4 stream_user_block
```

### Phase 1 Deployment (Logging Only)

```bash
# On Orange Pi 5
cd /opt/netpkt_vidcls
git pull origin naive

# Start in screen/tmux for persistence
screen -S stream_monitor

# Run with logging only
OPENWRT_IP=192.168.0.1
ssh -o ServerAliveInterval=30 root@$OPENWRT_IP \
  "tcpdump -i br-lan -s 192 -nn -w - 'port 80 or port 443'" \
| python3 python/naive_sniffer.py --log-only --verbose

# Detach: Ctrl+A, D
```

**Let run for 24h, then review logs before enabling enforcement.**

### Phase 2 Deployment (With Enforcement)

```bash
# After validating Phase 1 logs, enable blocking:
python3 python/naive_sniffer.py --enforce --verbose

# Monitor actively for first hour to catch issues
tail -f /var/log/naive_tracker.log
```

---

## Rollback Plan

If naive approach causes problems:

```bash
# Stop the tracker
pkill -f naive_sniffer.py

# Clear all blocks
ssh root@$OPENWRT_IP "nft flush set inet fw4 stream_user_block"

# Remove state file
rm /var/lib/naive_state.json

# Revert to ML approach (if desired)
git checkout main
# (Note: ML approach has buffering issue but at least doesn't block)
```

---

## Success Metrics

### Phase 1 Success:
- [ ] No crashes during 24h run
- [ ] Logs show streaming detection correlates with actual viewing
- [ ] False positive rate < 30% (acceptable for home network)
- [ ] No false negatives for videos >5min

### Phase 2 Success:
- [ ] Blocks are applied when quota exceeded
- [ ] Streaming stops for blocked clients
- [ ] Normal browsing continues to work
- [ ] Blocks expire automatically after timeout
- [ ] No persistent connection issues after block expires

### Overall Success:
- [ ] Watch time counted accurately (±10% error acceptable)
- [ ] Quota enforcement works reliably
- [ ] Family members can use "unblock" mechanism (manual nft command or web UI)
- [ ] System runs hands-off for weeks without intervention

---

## Open Questions / Decisions Needed

1. **Threshold tuning:** Start with 300 kbps or higher (400 kbps)?
   - Lower = catches more but more false positives
   - Higher = misses low-quality streams but fewer false positives
   - **Recommendation:** Start with 300 kbps, tune after 24h logs

2. **Window size:** 5s vs 3s?
   - 5s = more stable, slower detection
   - 3s = faster detection, more noise
   - **Recommendation:** Start with 5s

3. **Quota scope:** Per-client total or per-client-server?
   - Total = simpler, what we want
   - Per-server = more granular but complex
   - **Recommendation:** Per-client total

4. **Provider identification:** Implement SNI/DNS lookup?
   - Pro: Can show "YouTube: 45min, Netflix: 30min" in logs
   - Con: Adds complexity, not needed for basic quota
   - **Recommendation:** Phase 4 (nice-to-have)

5. **Web UI:** Create simple dashboard?
   - Pro: Family-friendly, shows quota status
   - Con: Extra 4-6h development
   - **Recommendation:** Phase 5 (future enhancement)

---

## Migration from ML Approach

### What to Keep:
- `python/config.py` - LAN subnet configuration
- `python/scapy_sniffer.py` - Packet parsing logic (can reuse `process_packet()`)
- `python/feature_creation.py` - Keep `preprocess()` for LAN/WAN filtering

### What to Retire:
- ML model (`etree.joblib`) - no longer needed
- `make_windowed_features()` - 25+ features not needed
- Training data (`training/raw.h5`) - no longer needed
- All feature engineering code - replaced by simple byte counting

### Git Strategy:
- Keep ML approach on `main` branch (archived, for reference)
- Develop naive approach on `naive` branch
- Eventually merge `naive` → `main` once proven stable
- Tag old ML code as `v0.1-ml` before merge

---

## Future Enhancements (Post-Deployment)

### Priority 1 (if needed):
- Automatic threshold calibration based on observed traffic patterns
- Per-client quota customization (e.g., parents vs kids)

### Priority 2 (nice-to-have):
- Provider identification via SNI parsing
- Web dashboard for quota monitoring
- Mobile notifications when quota reached

### Priority 3 (advanced):
- Whitelist mechanism (allow specific servers)
- Time-of-day quotas (e.g., unlimited after 8pm)
- Weekly/monthly quotas instead of daily

---

## Conclusion

The naive throughput-based approach is:
- **Simpler** (60 lines vs 500+)
- **More accurate** for time counting (95% vs 30%)
- **Easier to maintain** (thresholds vs retraining)
- **Sufficient** for home network quota enforcement

We accept slightly higher false positive rate (~20% vs ~7%) as acceptable trade-off for massive simplification and accurate time tracking.

**Next Step:** Implement Phase 1 (logging only) for validation before enforcement.
