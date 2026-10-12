/* Build & run: cc -I renderers tests/test_video_overflow.c -o /tmp/t && /tmp/t */
#include <assert.h>
#include <stdio.h>
#include "video_overflow.h"

static const unsigned char h264_idr[] = {0, 0, 0, 1, 0x67, 1, 2, 0, 0, 0, 1, 0x65, 9, 9, 9};
static const unsigned char h264_p[] = {0, 0, 0, 1, 0x41, 9, 9, 9, 9};
static const unsigned char h265_idr[] = {0, 0, 0, 1, 0x26, 1, 9, 9};   /* type 19 IDR_W_RADL */
static const unsigned char h265_p[] = {0, 0, 0, 1, 0x02, 1, 9, 9};     /* type 1 TRAIL_R */

int main(void) {
    assert(video_overflow_has_keyframe(h264_idr, sizeof h264_idr, false));
    assert(!video_overflow_has_keyframe(h264_p, sizeof h264_p, false));
    assert(video_overflow_has_keyframe(h265_idr, sizeof h265_idr, true));
    assert(!video_overflow_has_keyframe(h265_p, sizeof h265_p, true));
    assert(!video_overflow_has_keyframe(NULL, 0, false));

    bool dropping = false;
    /* under the cap: nothing dropped */
    assert(!video_overflow_should_drop(&dropping, 100, 1000, false));
    /* over the cap: drop this frame and every following non-key frame */
    assert(video_overflow_should_drop(&dropping, 5000, 1000, false));
    assert(dropping);
    assert(video_overflow_should_drop(&dropping, 10, 1000, false)); /* stays dropping even if level fell */
    /* a keyframe ends the run and is kept */
    assert(!video_overflow_should_drop(&dropping, 10, 1000, true));
    assert(!dropping);
    assert(!video_overflow_should_drop(&dropping, 10, 1000, false));
    /* a keyframe over the cap is still kept and never starts a drop run */
    assert(!video_overflow_should_drop(&dropping, 5000, 1000, true));
    assert(!dropping);
    /* AirPlay AVCC (4-byte big-endian length) -> Annex-B, exactly as the engine's
     * raop_rtp_mirror.c loop does before frames reach the renderer. */
    unsigned char avcc[] = {0, 0, 0, 3, 0x67, 1, 2,  0, 0, 0, 4, 0x65, 9, 9, 9};
    static const unsigned char start_code[4] = {0, 0, 0, 1};
    int off = 0;
    while (off < (int) sizeof avcc) {
        int len = (avcc[off] << 24) | (avcc[off + 1] << 16) | (avcc[off + 2] << 8) | avcc[off + 3];
        for (int i = 0; i < 4; i++) avcc[off + i] = start_code[i];
        off += 4 + len;
    }
    assert(video_overflow_has_keyframe(avcc, sizeof avcc, false));
    /* P frame converted the same way is not a keyframe */
    unsigned char avcc_p[] = {0, 0, 0, 4, 0x41, 9, 9, 9};
    for (int i = 0; i < 4; i++) avcc_p[i] = start_code[i];
    assert(!video_overflow_has_keyframe(avcc_p, sizeof avcc_p, false));

    /* the filter scans lazily: under the cap and not dropping, an IDR-looking
     * buffer is passed untouched and the state stays clean */
    dropping = false;
    assert(!video_overflow_filter(&dropping, 10, 1000, h264_p, sizeof h264_p, false));
    assert(!dropping);
    assert(video_overflow_filter(&dropping, 5000, 1000, h264_p, sizeof h264_p, false));
    assert(video_overflow_filter(&dropping, 10, 1000, h264_p, sizeof h264_p, false));
    assert(!video_overflow_filter(&dropping, 10, 1000, h264_idr, sizeof h264_idr, false));
    assert(!dropping);
    puts("video_overflow: ok");
    return 0;
}
