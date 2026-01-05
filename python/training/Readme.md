To create training samples data files

```bash
sudo tcpdump -i wlp2s0 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py --train > youtube_packets_01.txt
```

#### List of training Data

Files parsed already ingested by raw.h5 pandas dataframe:

- working_packets_01.txt web activities: spotify piano playing, teams, whatsapp
- working_packets_02.txt web activities: unimed, teams anp, outlook, drive, sei, google search ...
- working_packets_03.txt web activities: nothing: locked screen
- youtube_packets_01.txt web activities: youtube real crusades history 11 minutes
- youtube_packets_02.txt web activities: youtube veritasium 20 minutes
- youtube_packets_03.txt web activities: youtube shorts for some minutes > 10 minutes
- insta_packets_01.txt web activities: instagram scrolling and videos
- insta_packets_02.txt web activities: instagram scrolling and videos



#### Needed


- Audio streaming (audible, spotify, pilgrim) only to classify as not video streaming

---

## Naive Tracker Baseline Results (Phase 1)

**Date:** 2026-01-05
**Config:** `rate_threshold=50KB/s`, `window_size=5s`, `consecutive_windows=3`

| Scenario | Label | Duration | Detection Rate |
|----------|-------|----------|----------------|
| youtube_packets_01 | streaming | 760s | 32.4% |
| youtube_packets_02 | streaming | 1446s | 36.6% |
| youtube_packets_03 | streaming | 597s | 25.5% |
| insta_packets_02 | streaming | 559s | 37.0% |
| insta_packets_01 | streaming | 327s | 100.0%* |
| working_packets_01 | not streaming | 2023s | 2.6% |
| working_packets_02 | not streaming | 1588s | 4.8% |
| working_packets_03 | not streaming | 1621s | 0.0% |

*insta_packets_01 has data anomaly (watch_time >> duration)

**Summary:**
- Avg streaming detection: **46.3%** (low due to ABR buffering)
- Avg false positive: **2.5%** (good)

**Why low detection?** Modern video uses adaptive bitrate (ABR) streaming:
- Burst phase (2-3s): Downloads 6s of video at high speed
- Silent phase (3-4s): Player buffers, no network traffic
- Naive tracker only counts burst time → misses ~65% of actual viewing

**Test script:** `test_naive_with_training_data.py`
**Visualization:** `naive_validation_results.png`

---

## Phase 3 Experiments: EWMA + Buffer-Credit (2026-01-05)

**Goal:** Improve streaming detection from ~30% to >60% without increasing false positives.

**Result:** Mixed - detection improved but false positives increased unacceptably.

### Configurations Tested

| Config | use_ewma | ewma_alpha | use_buffer_credit | max_buffer |
|--------|----------|------------|-------------------|------------|
| Baseline | False | - | False | - |
| EWMA α=0.3 | True | 0.3 | False | - |
| EWMA α=0.1 | True | 0.1 | False | - |
| EWMA + Buffer | True | 0.3 | True | 30-90s |

### Per-Scenario Results

| Scenario | Label | Baseline | EWMA α=0.3 | EWMA α=0.1 |
|----------|-------|----------|------------|------------|
| youtube_packets_01 | streaming | 32.4% | 37.2% | 37.2% |
| youtube_packets_02 | streaming | 36.6% | 40.8% | 40.4% |
| youtube_packets_03 | streaming | 25.5% | 37.1% | 34.4% |
| insta_packets_01 | streaming | 14.8% | 74.1% | 74.1% |
| insta_packets_02 | streaming | 37.0% | 68.0% | 67.1% |
| working_packets_01 | NOT streaming | **2.6%** | 25.8% | 31.7% |
| working_packets_02 | NOT streaming | **4.8%** | 22.6% | 28.5% |
| working_packets_03 | NOT streaming | **0.0%** | 0.0% | 0.0% |

### Summary

| Config | Streaming Avg | False Positive Avg |
|--------|---------------|-------------------|
| **Baseline** | 29.3% | **2.5%** |
| EWMA α=0.3 | 51.4% | 16.1% |
| EWMA α=0.1 | 50.6% | 20.1% |

### Key Findings

1. **EWMA increases both detection AND false positives** - not a clear win
2. **Lower alpha = worse** - more smoothing keeps rates elevated longer, causing MORE false positives
3. **Buffer credit had no measurable effect** - after fixing to only accumulate when streaming, it didn't help
4. **Instagram improved dramatically** with EWMA (14.8% → 74.1%)
5. **YouTube improvement modest** (32% → 37-41%)
6. **Working activity false positives unacceptable** (2.5% → 16-20%)

### Conclusion

The Phase 3 approach trades false positive rate for detection rate. For quota enforcement, low false positives are critical (don't want to penalize users for normal browsing).

**Recommendation:** Keep baseline configuration (no EWMA, no buffer credit) until a better approach is found. The code for EWMA and buffer-credit is preserved in naive_tracker.py with config flags disabled.