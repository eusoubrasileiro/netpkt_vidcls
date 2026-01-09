/*
 * Unit tests for protocol detection functions
 */

#include <criterion/criterion.h>
#include "streamguard_test.h"

/* Test is_social_media_protocol() */

Test(social_media, instagram_is_social) {
    ndpi_protocol proto = {0};
    proto.proto.app_protocol = NDPI_PROTOCOL_INSTAGRAM;
    cr_assert(is_social_media_protocol(proto) == 1,
              "Instagram should be detected as social media");
}

Test(social_media, facebook_is_social) {
    ndpi_protocol proto = {0};
    proto.proto.app_protocol = NDPI_PROTOCOL_FACEBOOK;
    cr_assert(is_social_media_protocol(proto) == 1,
              "Facebook should be detected as social media");
}

Test(social_media, facebook_reel_is_social) {
    ndpi_protocol proto = {0};
    proto.proto.app_protocol = NDPI_PROTOCOL_FACEBOOK_REEL_STORY;
    cr_assert(is_social_media_protocol(proto) == 1,
              "Facebook Reel/Story should be detected as social media");
}

Test(social_media, youtube_is_not_social) {
    ndpi_protocol proto = {0};
    proto.proto.app_protocol = NDPI_PROTOCOL_YOUTUBE;
    cr_assert(is_social_media_protocol(proto) == 0,
              "YouTube should NOT be detected as social media");
}

Test(social_media, http_is_not_social) {
    ndpi_protocol proto = {0};
    proto.proto.app_protocol = NDPI_PROTOCOL_HTTP;
    cr_assert(is_social_media_protocol(proto) == 0,
              "HTTP should NOT be detected as social media");
}

/* Test is_trackable_traffic() */

Test(trackable, video_category_is_trackable) {
    ndpi_protocol proto = {0};
    proto.category = NDPI_PROTOCOL_CATEGORY_VIDEO;
    cr_assert(is_trackable_traffic(proto) == 1,
              "VIDEO category should be trackable");
}

Test(trackable, streaming_category_is_trackable) {
    ndpi_protocol proto = {0};
    proto.category = NDPI_PROTOCOL_CATEGORY_STREAMING;
    cr_assert(is_trackable_traffic(proto) == 1,
              "STREAMING category should be trackable");
}

Test(trackable, media_category_is_trackable) {
    ndpi_protocol proto = {0};
    proto.category = NDPI_PROTOCOL_CATEGORY_MEDIA;
    cr_assert(is_trackable_traffic(proto) == 1,
              "MEDIA category should be trackable");
}

Test(trackable, web_category_not_trackable) {
    ndpi_protocol proto = {0};
    proto.category = NDPI_PROTOCOL_CATEGORY_WEB;
    proto.proto.app_protocol = NDPI_PROTOCOL_HTTP;
    cr_assert(is_trackable_traffic(proto) == 0,
              "WEB category should NOT be trackable");
}

Test(trackable, social_trackable_in_default_mode) {
    test_set_video_only_mode(0);  /* Default mode */
    ndpi_protocol proto = {0};
    proto.proto.app_protocol = NDPI_PROTOCOL_INSTAGRAM;
    proto.category = NDPI_PROTOCOL_CATEGORY_SOCIAL_NETWORK;
    cr_assert(is_trackable_traffic(proto) == 1,
              "Instagram should be trackable in default (ALL-SOCIAL) mode");
}

Test(trackable, social_not_trackable_in_video_only) {
    test_set_video_only_mode(1);  /* Video-only mode */
    ndpi_protocol proto = {0};
    proto.proto.app_protocol = NDPI_PROTOCOL_INSTAGRAM;
    proto.category = NDPI_PROTOCOL_CATEGORY_SOCIAL_NETWORK;
    cr_assert(is_trackable_traffic(proto) == 0,
              "Instagram should NOT be trackable in VIDEO-ONLY mode");
    test_set_video_only_mode(0);  /* Reset */
}

Test(trackable, youtube_trackable_via_category) {
    ndpi_protocol proto = {0};
    proto.proto.app_protocol = NDPI_PROTOCOL_YOUTUBE;
    proto.category = NDPI_PROTOCOL_CATEGORY_VIDEO;
    cr_assert(is_trackable_traffic(proto) == 1,
              "YouTube should be trackable via VIDEO category");
}
