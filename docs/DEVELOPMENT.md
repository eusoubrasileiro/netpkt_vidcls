# StreamGuard Development Notes

Technical details for developers working on StreamGuard internals.

## nDPI 5.0 API

StreamGuard uses nDPI 5.0 which differs from older versions:

```c
/* Initialization - all protocols enabled by default */
ndpi_module = ndpi_init_detection_module(NULL);
ndpi_finalize_initialization(ndpi_module);

/* Detection - extra input_info parameter */
ndpi_detection_process_packet(ndpi_module, flow, packet, len, time_ms, NULL);

/* Giveup - only 2 parameters */
ndpi_detection_giveup(ndpi_module, flow);

/* Protocol access */
flow->detected_protocol.proto.app_protocol
flow->detected_protocol.category
```

## Protocol Detection

### Category-based (always tracked)

```c
if (proto.category == NDPI_PROTOCOL_CATEGORY_VIDEO ||
    proto.category == NDPI_PROTOCOL_CATEGORY_STREAMING ||
    proto.category == NDPI_PROTOCOL_CATEGORY_MEDIA) {
    return 1;  /* trackable */
}
```

### Social media (unless -V flag)

Instagram and Facebook are classified as `SOCIAL_NETWORK`, not VIDEO:

```c
static int is_social_media_protocol(ndpi_protocol proto) {
    uint16_t app = proto.proto.app_protocol;
    return (app == NDPI_PROTOCOL_INSTAGRAM ||
            app == NDPI_PROTOCOL_FACEBOOK ||
            app == NDPI_PROTOCOL_FACEBOOK_REEL_STORY);
}
```

## Session Tracking

- Session starts on first tracked packet
- Each packet resets the 45-second inactivity timer
- Session ends after 45s of no packets
- Duration = last_activity - session_start

## Remote Capture (rpcapd)

rpcapd is libpcap's Remote Capture Protocol daemon. Ubuntu's libpcap package lacks remote support, so we build locally with `--enable-remote`.

The `HAVE_REMOTE` macro enables rpcap:// URL support in streamguard.c.

## Flow Hashing

Flows are normalized (smaller IP first) so both directions hash to the same entry:

```c
static void normalize_flow_key(uint32_t *ip1, uint32_t *ip2,
                                uint16_t *port1, uint16_t *port2) {
    if (*ip1 > *ip2 || (*ip1 == *ip2 && *port1 > *port2)) {
        /* swap */
    }
}
```

## Testing

Unit tests use the `TESTING` macro to expose static functions:

```c
#ifdef TESTING
#define STATIC  /* expose for testing */
#else
#define STATIC static
#endif
```

Test pcap files are in `test/pcaps/`.
