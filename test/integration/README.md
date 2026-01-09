# StreamGuard Tests

Pcap-based unit tests using real network captures.

## Run Tests

```bash
cd src && make test
```

## Test Cases

| Pcap | Tests | Validates |
|------|-------|-----------|
| youtube_45sec.pcap | YouTube detection, Session start, Session end | TLS.YouTube, SESSION_START/END |
| instagram_45sec.pcap | Instagram detection, Video-only mode | Social media, `-V` flag ignores it |
| multi_client.pcap | Multi-client tracking | Per-client sessions |
| browsing_30sec.pcap | Browsing not tracked | Non-streaming ignored |

## Capture Test Pcaps

All captures use `-s 1024` (headers only) and `timeout` (auto-stop).

```bash
cd test/pcaps

# 1. YouTube - watch a YouTube video during capture
sudo timeout 45 tcpdump -i eno1 -s 1024 -w youtube_45sec.pcap

# 2. Instagram via router - scroll Instagram on Android
timeout 45 ssh root@192.168.0.1 "tcpdump -i br-lan -s 1024 -w -" > instagram_45sec.pcap

# 3. Multi-client via router - multiple devices streaming simultaneously
timeout 45 ssh root@192.168.0.1 "tcpdump -i br-lan -s 1024 -w -" > multi_client.pcap

# 4. Browsing negative test - browse Wikipedia/news (no streaming)
sudo timeout 30 tcpdump -i eno1 -s 1024 -w browsing_30sec.pcap
```

## Why `-s 1024`?

StreamGuard only needs packet headers for nDPI detection:
- Ethernet + IP + TCP/UDP headers (~60 bytes)
- TLS ClientHello with SNI (~200-300 bytes)
- QUIC Initial with encrypted SNI (~400-500 bytes)

**Use 1024 bytes** for safety margin. QUIC (modern YouTube) needs ~500 bytes for nDPI to decrypt and extract SNI. With only 256 bytes, QUIC is detected as generic "QUIC" instead of "QUIC.YouTube".

Full video payload is ignored, so 1024 bytes keeps pcaps small (~30-40 MB per 30 sec instead of gigabytes).

## Truncate Existing Large Pcaps

```bash
editcap -s 1024 large_capture.pcap small_capture.pcap
```
