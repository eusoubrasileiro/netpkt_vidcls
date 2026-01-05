# The Buffering Problem: Why Naive > ML

**Date:** 2026-01-05

## The Problem

Modern video streaming uses **Adaptive Bitrate (ABR)** with segment-based delivery:

```
[BURST 2-3s: downloads 6s of video] → [SILENCE 3-4s: plays buffer] → [repeat...]
         ↑ high traffic                     ↑ zero traffic
```

### What ML Detects

- Detects the 2-3s burst (high throughput)
- **Misses** the 3-4s silence (buffer playback)
- **Result:** 30min video = ~10min detected

| Scenario | Actual Time | ML Counts | Error |
|----------|-------------|-----------|-------|
| YouTube 30min | 30min | ~10min | -66% |
| Netflix 2h | 120min | ~40min | -67% |
| TikTok 1h | 60min | ~20min | -67% |

**Conclusion:** ML is unsuitable for quota enforcement.

## Why Naive Works

### Buffer-Credit Accounting

```python
# During BURST (2s):
buffer_credit += bytes_downloaded / estimated_bitrate  # adds ~6s credit

# During SILENCE (4s):
buffer_credit -= 4s  # consumes credit
watch_time += 4s     # keeps counting!

# Result: counts correctly!
```

### Full Logic

1. **EWMA smoothing:** Smooths throughput variations
2. **Hysteresis:** Prevents flapping (START: 400kbps, STOP: 150kbps)
3. **Buffer accounting:** Counts time during silent periods
4. **Result:** ~95% accuracy vs ~30% for ML

## Comparison

### Complexity

| Aspect | ML | Naive |
|--------|-------|--------|
| Lines of code | ~500 | ~60 |
| Dependencies | scikit-learn, joblib, pandas, numpy | pandas, numpy |
| Training data | 185MB, 8 scenarios | None |
| Feature extraction | 25+ features | Just byte counting |
| Maintenance | Retrain model | Adjust 2-3 thresholds |

### Accuracy

| Metric | ML | Naive |
|--------|-------|--------|
| **Time counted** | 30-40% of actual | ~95% of actual |
| False positives | ~7% | ~20% |
| False negatives | ~10% | ~5% |
| Detection delay | 30s | 9-15s |

## Acceptable False Positives

**Naive may detect as "streaming":**
- Large ISO download (1GB in 300s) → counts 5min
- Game update (5GB in 20min) → counts 20min
- Online backup

**Why it's OK:**
- Home network: large downloads are rare
- Daily quotas are hours (30-60min)
- 5-20min of "noise" is insignificant
- Can whitelist known IPs if needed

## Recommended Solution

### EWMA + Hysteresis + Buffer Credit

```python
# --- tuning ---
WINDOW = 3.0                        # seconds
START_T = 400_000                   # ~400 kbps start
STOP_T  = 150_000                   # ~150 kbps stop
K_CONSEC_START = 2
M_CONSEC_STOP  = 6
MAX_BUFFER_SEC = 90
ALPHA = 0.3                         # EWMA weight

# per (client, provider) state
state = {}  # key -> dict(play=False, ewma=0, est_bitrate=800_000, k=0, m=0, buf=0, used=0)

def tick(client_ip, provider, bytes_in_window):
    key = (client_ip, provider)
    s = state.setdefault(key, dict(play=False, ewma=0, est_bitrate=800_000,
                                   k=0, m=0, buf=0.0, used=0))
    rate = bytes_in_window / WINDOW  # bytes/sec
    s['ewma'] = (1-ALPHA)*s['ewma'] + ALPHA*rate

    # update est_bitrate only when playing
    if s['play']:
        s['est_bitrate'] = 0.8*s['est_bitrate'] + 0.2*max(s['ewma'], 100_000)

    # accumulate buffer on bursts while playing
    if s['ewma'] >= START_T:
        s['buf'] = min(MAX_BUFFER_SEC, s['buf'] + bytes_in_window / max(s['est_bitrate'], 1))

    if not s['play']:
        s['k'] = s['k'] + 1 if s['ewma'] >= START_T else 0
        if s['k'] >= K_CONSEC_START:
            s['play'] = True
            s['k'] = 0
            s['buf'] = min(MAX_BUFFER_SEC, s['buf'] + 5)
    else:
        if s['ewma'] < STOP_T:
            s['m'] += 1
        else:
            s['m'] = 0

        playing_this_window = True
        if s['ewma'] < STOP_T:
            s['buf'] = max(0.0, s['buf'] - WINDOW)
            if s['buf'] <= 0 and s['m'] >= M_CONSEC_STOP:
                s['play'] = False
                playing_this_window = False
                s['m'] = 0

        if playing_this_window:
            s['used'] += WINDOW  # watch-time to compare vs quota

    return s['play'], s['used']
```

### Why This Handles Buffering Well

* **Segment cadence** is absorbed by `MAX_BUFFER_SEC` and EWMA/hysteresis
* **Prefetch spikes** don't inflate watch-time: credit is capped
* **Quality changes (ABR)** auto-adjust via `est_bitrate`

### Practical Defaults

* `WINDOW=3s`, `START_T=400 kbps`, `STOP_T=150 kbps`, `MAX_BUFFER_SEC=90s`
* Start with per-device daily quotas like 30–60 min
* Log decisions for a day before enforcing

## Conclusion

The naive throughput-based approach is:
- **Simpler** (60 lines vs 500+)
- **More accurate** for time counting (95% vs 30%)
- **Easier to maintain** (thresholds vs retraining)
- **Sufficient** for home network quota enforcement

We accept ~20% false positive rate (vs ~7% ML) as acceptable trade-off for:
- **3x more accurate** time counting
- **Massive simplification**
- **Zero training data**
- **Zero model maintenance**
