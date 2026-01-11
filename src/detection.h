/*
 * Detection - nDPI wrapper for protocol classification
 */

#ifndef DETECTION_H
#define DETECTION_H

#include <ndpi_api.h>

/* Initialize nDPI detection module */
struct ndpi_detection_module_struct *detection_init(void);

/* Cleanup nDPI detection module */
void detection_exit(struct ndpi_detection_module_struct *module);

/* Check if protocol is a social media platform (Instagram, Facebook) */
int det_is_social_media_protocol(ndpi_protocol proto);

/* Check if traffic should be tracked for quota
 * video_only_mode: 0 = track social media too, 1 = only video categories
 */
int det_is_trackable_traffic(ndpi_protocol proto, int video_only_mode);

#endif /* DETECTION_H */
