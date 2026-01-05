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
| insta_packets_01 | streaming | 327s | 48.3% |
| working_packets_01 | not streaming | 2023s | 2.6% |
| working_packets_02 | not streaming | 1588s | 4.8% |
| working_packets_03 | not streaming | 1621s | 0.0% |

**Summary:**
- Avg streaming detection: **36.0%** (low due to ABR buffering)
- Avg false positive: **2.5%** (good)
- Balanced Accuracy: **66.7%**

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

---

## Phase 4: Buffer Credit Model with Grid Search (2026-01-05)

**Goal:** Replace consecutive-windows threshold with a buffer credit model that tracks accumulated video seconds.

**Algorithm:**
```python
seconds_downloaded = bytes_in_window / video_bitrate  # Convert bytes to video time
seconds_drained = drain_rate * window_size           # Constant playback consumption
buffer_credit = max(0, buffer_credit + seconds_downloaded - seconds_drained)

# State transitions:
if buffer_credit >= entry_threshold: is_streaming = True
if buffer_credit <= 0: is_streaming = False
```

### Grid Search Results (72 configurations tested)

**Parameters searched:**
- `video_bitrate`: 400K, 560K, 750K bytes/sec
- `drain_rate`: 0.3, 0.4, 0.5, 0.6, 0.7, 0.8
- `entry_threshold`: 5, 10, 15, 20 seconds

### Comprehensive Metrics Comparison

| Config | TPR (Detection) | FPR (False Pos) | TNR (Specificity) | FNR (Miss Rate) | Balanced Acc | Score |
|--------|-----------------|-----------------|-------------------|-----------------|--------------|-------|
| **Baseline** | 36.0% | **2.5%** | **97.5%** | 64.0% | 66.7% | **31.0** |
| Buffer #1 (400K/0.8/5s) | **48.4%** | 9.4% | 90.6% | **51.6%** | **69.5%** | 29.7 |
| Buffer #2 (560K/0.6/20s) | 38.8% | 4.8% | 95.2% | 61.2% | 67.0% | 29.2 |
| Buffer #3 (560K/0.6/15s) | 40.0% | 5.4% | 94.6% | 60.0% | 67.3% | 29.2 |
| Buffer #4 (560K/0.6/10s) | 40.8% | 6.3% | 93.7% | 59.2% | 67.2% | 28.1 |
| Buffer #5 (750K/0.5/5s) | 41.3% | 6.5% | 93.5% | 58.7% | 67.4% | 28.4 |

### Per-Scenario Detection Rates

| Scenario | Label | Baseline | Buffer #1 | Buffer #2 | Buffer #3 |
|----------|-------|----------|-----------|-----------|-----------|
| youtube_packets_01 | streaming | 32.4% | 33.7% | 31.7% | 31.7% |
| youtube_packets_02 | streaming | 36.6% | 39.3% | 33.6% | 36.0% |
| youtube_packets_03 | streaming | 25.5% | **38.0%** | 23.9% | 27.6% |
| insta_packets_01 | streaming | 48.3% | **66.8%** | 52.0% | 52.0% |
| insta_packets_02 | streaming | 37.0% | **64.1%** | 52.7% | 52.7% |
| working_packets_01 | NOT | **2.6%** | 20.9% ❌ | 12.5% | 14.3% |
| working_packets_02 | NOT | **4.8%** | 7.1% | **2.0%** | **2.0%** |
| working_packets_03 | NOT | 0.0% | 0.0% | 0.0% | 0.0% |

### Key Findings

1. **Buffer model improves detection** from 36% to 48% (Buffer #1), but increases FP from 2.5% to 9.4%
2. **Best balanced accuracy** is Buffer #1 at 69.5% (vs baseline 66.7%)
3. **Instagram detection improved significantly**: 37-48% → 64-67%
4. **YouTube detection modest improvement**: 25-37% → 33-39%
5. **`working_packets_01` is problematic** - contains Spotify audio streaming which triggers false positives in all buffer configs
6. **Trade-off**: Lower `entry_threshold` = better detection but higher FP

### Metrics Explained

- **TPR (True Positive Rate)**: % of streaming correctly detected
- **FPR (False Positive Rate)**: % of non-streaming incorrectly flagged
- **TNR (True Negative Rate)**: % of non-streaming correctly ignored (100 - FPR)
- **FNR (False Negative Rate)**: % of streaming missed (100 - TPR)
- **Balanced Accuracy**: (TPR + TNR) / 2
- **Score**: TPR - 2×FPR (penalizes false positives)

### Conclusion

The buffer credit model achieves **higher balanced accuracy** (69.5% vs 66.7%) but at the cost of increased false positives. The baseline's ultra-low FPR (2.5%) gives it a higher score on the penalty-weighted metric.

**Recommendation:**
- For **quota enforcement** (low FP priority): Use baseline or Buffer #2 (560K/0.6/20s)
- For **maximum detection**: Use Buffer #1 (400K/0.8/5s)
- **Critical need**: Audio streaming training data to distinguish from video

**Test script:** `param_search.py`
**Results:** `param_search_results.csv`