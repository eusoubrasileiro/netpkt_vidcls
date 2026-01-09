#!/bin/bash
# test/run_tests.sh - StreamGuard pcap-based unit tests
#
# Test pcaps:
#   youtube_45sec.pcap    - YouTube video from PC (192.168.0.25)
#   instagram_45sec.pcap  - Instagram from Android via router (192.168.0.122)
#   multi_client.pcap     - Multiple clients via router
#   browsing_30sec.pcap   - Non-streaming web browsing (negative test)

STREAMGUARD="../src/streamguard"
PASS=0; FAIL=0

test() {
    local name="$1" pcap="$2" pattern="$3" flags="${4:-}" expect="${5:-yes}"
    printf "%-45s" "$name..."

    if [ ! -f "pcaps/$pcap" ]; then
        echo "SKIP (pcap not found)"
        return
    fi

    out=$($STREAMGUARD -r "pcaps/$pcap" $flags 2>&1)

    if echo "$out" | grep -q "$pattern"; then matched=yes; else matched=no; fi

    if [ "$matched" = "$expect" ]; then
        echo "PASS"; ((PASS++))
    else
        echo "FAIL"; ((FAIL++))
        echo "  Expected: $expect, Got: $matched"
        echo "  Pattern: $pattern"
    fi
}

# Test quota measurement is within expected range
test_quota() {
    local name="$1" pcap="$2" min="$3" max="$4"
    printf "%-45s" "$name..."

    if [ ! -f "pcaps/$pcap" ]; then
        echo "SKIP (pcap not found)"
        return
    fi

    out=$($STREAMGUARD -r "pcaps/$pcap" 2>&1)
    # Extract total seconds from "total: NN sec" or "NN seconds"
    secs=$(echo "$out" | grep -oE "total: [0-9]+ sec" | tail -1 | grep -oE "[0-9]+")

    if [ -z "$secs" ]; then
        echo "FAIL (no quota found)"
        ((FAIL++))
        return
    fi

    if [ "$secs" -ge "$min" ] && [ "$secs" -le "$max" ]; then
        echo "PASS (${secs}s)"
        ((PASS++))
    else
        echo "FAIL (${secs}s not in ${min}-${max}s range)"
        ((FAIL++))
    fi
}

echo "=== StreamGuard Tests ==="
echo

# --- youtube_45sec.pcap ---
test "YouTube detection"            youtube_45sec.pcap    "YouTube"
test "Session start"                youtube_45sec.pcap    "SESSION_START"
test "Session end"                  youtube_45sec.pcap    "SESSION_END"

# --- instagram_45sec.pcap ---
test "Instagram detection"          instagram_45sec.pcap  "Instagram"
test "Video-only ignores Instagram" instagram_45sec.pcap  "Instagram" "-V" "no"

# --- multi_client.pcap ---
test "Multi-client tracking"        multi_client.pcap     "SESSION_START"

# --- browsing_30sec.pcap (negative test) ---
test "Browsing not tracked"         browsing_30sec.pcap   "STREAMING" "" "no"

# --- Quota measurement tests ---
test_quota "YouTube quota (30-50s expected)"   youtube_45sec.pcap   30 50
test_quota "Instagram quota (30-50s expected)" instagram_45sec.pcap 30 50

echo
echo "=== $PASS passed, $FAIL failed ==="
[ $FAIL -eq 0 ]
