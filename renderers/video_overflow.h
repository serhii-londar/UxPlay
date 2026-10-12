/*
 * Keyframe-aware overflow policy for the compressed video queue in front of the
 * decoder (PIPE-07 / #67).
 *
 * The AirPlay mirror stream is H.264/H.265 over a one-way TCP socket: the sender
 * offers no "send me a keyframe" request, and /feedback is the sender's own
 * heartbeat. So a receiver that falls behind cannot ask for a fresh IDR; it can
 * only drop compressed frames and wait for the next one the sender emits.
 * Dropping an arbitrary frame corrupts every following P-frame until that IDR,
 * so once the backlog passes the cap we drop *everything* up to the next IDR
 * (a coherent gap, not a smeared picture) and resume there.
 *
 * Header-only and glib/gstreamer free so it can be unit-tested on its own.
 */
#ifndef VIDEO_OVERFLOW_H
#define VIDEO_OVERFLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Backlog (bytes) held by appsrc, behind a shallow queue, before frames are shed. */
#define VIDEO_APPSRC_MAX_BYTES (2u * 1024u * 1024u)
/* Buffers allowed in the queue between appsrc and the parser; keeps the backlog
 * visible to appsrc (where it is measured) instead of hidden in a deep queue. */
#define VIDEO_QUEUE_MAX_BUFFERS 8

/* True when the Annex-B access unit [data, data+len) contains an IDR picture:
 * H.264 NAL type 5, H.265 NAL types 16..21 (BLA/IDR/CRA, all random access points). */
static inline bool video_overflow_has_keyframe(const unsigned char *data, size_t len, bool h265) {
    if (!data) {
        return false;
    }
    for (size_t i = 0; i + 4 < len; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            unsigned char b = data[i + 3];
            if (h265) {
                unsigned type = (b >> 1) & 0x3f;
                if (type >= 16 && type <= 21) {
                    return true;
                }
            } else if ((b & 0x1f) == 5) {
                return true;
            }
            i += 2;
        }
    }
    return false;
}

/* Decides whether to discard the incoming frame. [dropping] is per-stream state.
 * Keyframes are never discarded: they end a drop run and are always worth decoding. */
static inline bool video_overflow_should_drop(bool *dropping, uint64_t level_bytes, uint64_t max_bytes,
                                              bool is_keyframe) {
    if (is_keyframe) {
        *dropping = false;
        return false;
    }
    if (*dropping) {
        return true;
    }
    if (level_bytes > max_bytes) {
        *dropping = true;
        return true;
    }
    return false;
}

#endif /* VIDEO_OVERFLOW_H */
