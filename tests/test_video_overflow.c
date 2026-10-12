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
    puts("video_overflow: ok");
    return 0;
}
