/*
 * Detection - nDPI wrapper for protocol classification
 */

#include "detection.h"
#include "log.h"

/* Initialize nDPI detection module */
struct ndpi_detection_module_struct *detection_init(void) {
    struct ndpi_detection_module_struct *module = ndpi_init_detection_module(NULL);
    if (!module) {
        log_fatal("Failed to initialize nDPI");
        return NULL;
    }

    ndpi_finalize_initialization(module);
    log_info("nDPI initialized (version %s)", ndpi_revision());

    return module;
}

/* Cleanup nDPI detection module */
void detection_exit(struct ndpi_detection_module_struct *module) {
    if (module) {
        ndpi_exit_detection_module(module);
    }
}

/* Check if protocol is a social media platform (Instagram, Facebook) */
int det_is_social_media_protocol(ndpi_protocol proto) {
    uint16_t app = proto.proto.app_protocol;
    return (app == NDPI_PROTOCOL_INSTAGRAM ||
            app == NDPI_PROTOCOL_FACEBOOK ||
            app == NDPI_PROTOCOL_FACEBOOK_REEL_STORY);
}

/* Check if traffic should be tracked for quota
 * Default: VIDEO/STREAMING/MEDIA categories + social media (Instagram, Facebook)
 * Video-only mode (-V): Only VIDEO/STREAMING/MEDIA categories
 */
int det_is_trackable_traffic(ndpi_protocol proto, int video_only_mode) {
    /* Always track pure video/streaming categories */
    if (proto.category == NDPI_PROTOCOL_CATEGORY_VIDEO ||
        proto.category == NDPI_PROTOCOL_CATEGORY_STREAMING ||
        proto.category == NDPI_PROTOCOL_CATEGORY_MEDIA) {
        return 1;
    }
    /* Track social media unless in video-only mode */
    if (!video_only_mode && det_is_social_media_protocol(proto)) {
        return 1;
    }
    return 0;
}
