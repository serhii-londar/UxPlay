/* Build & run: cc -I lib tests/test_auth_throttle.c -o /tmp/t && /tmp/t */
#include <assert.h>
#include <stdio.h>
#include "auth_throttle.h"

static const unsigned char host_a[4] = {192, 168, 1, 40};
static const unsigned char host_b[4] = {192, 168, 1, 41};
static const unsigned char host_v6[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};

int main(void) {
    auth_throttle_t t;
    memset(&t, 0, sizeof t);
    uint64_t now = 1000;

    /* the first AUTH_THROTTLE_FREE_FAILURES wrong attempts cost nothing */
    for (int i = 0; i < AUTH_THROTTLE_FREE_FAILURES; i++) {
        assert(auth_throttle_record_failure(&t, host_a, 4, now) == 0);
        assert(!auth_throttle_is_locked(&t, host_a, 4, now));
    }

    /* the next one locks the address out for the base time; others are unaffected */
    assert(auth_throttle_record_failure(&t, host_a, 4, now) == AUTH_THROTTLE_BASE_LOCK);
    assert(auth_throttle_is_locked(&t, host_a, 4, now));
    assert(auth_throttle_remaining(&t, host_a, 4, now) == AUTH_THROTTLE_BASE_LOCK);
    assert(!auth_throttle_is_locked(&t, host_b, 4, now));
    assert(!auth_throttle_is_locked(&t, host_v6, 16, now));

    /* it counts down and then ends */
    assert(auth_throttle_remaining(&t, host_a, 4, now + 10) == AUTH_THROTTLE_BASE_LOCK - 10);
    now += AUTH_THROTTLE_BASE_LOCK + 1;
    assert(!auth_throttle_is_locked(&t, host_a, 4, now));

    /* each further wrong attempt doubles the lockout ... */
    assert(auth_throttle_record_failure(&t, host_a, 4, now) == 2 * AUTH_THROTTLE_BASE_LOCK);
    now += 2 * AUTH_THROTTLE_BASE_LOCK + 1;
    assert(auth_throttle_record_failure(&t, host_a, 4, now) == 4 * AUTH_THROTTLE_BASE_LOCK);

    /* ... up to the cap */
    uint32_t last = 0;
    for (int i = 0; i < 40; i++) {
        now += AUTH_THROTTLE_MAX_LOCK + 1;
        last = auth_throttle_record_failure(&t, host_a, 4, now);
    }
    assert(last == AUTH_THROTTLE_MAX_LOCK);
    assert(auth_throttle_remaining(&t, host_a, 4, now) == AUTH_THROTTLE_MAX_LOCK);

    /* a correct attempt clears the address: five wrong ones are free again */
    auth_throttle_record_success(&t, host_a, 4);
    assert(!auth_throttle_is_locked(&t, host_a, 4, now));
    for (int i = 0; i < AUTH_THROTTLE_FREE_FAILURES; i++) {
        assert(auth_throttle_record_failure(&t, host_a, 4, now) == 0);
    }
    assert(!auth_throttle_is_locked(&t, host_a, 4, now));

    /* an address that stays quiet is forgotten, and starts from zero again */
    assert(auth_throttle_record_failure(&t, host_b, 4, now) == 0);
    for (int i = 0; i < AUTH_THROTTLE_FREE_FAILURES; i++) auth_throttle_record_failure(&t, host_b, 4, now);
    assert(auth_throttle_is_locked(&t, host_b, 4, now));
    now += AUTH_THROTTLE_FORGET_AFTER + 1;
    assert(!auth_throttle_is_locked(&t, host_b, 4, now));
    for (int i = 0; i < AUTH_THROTTLE_FREE_FAILURES; i++) {
        assert(auth_throttle_record_failure(&t, host_b, 4, now) == 0);
    }

    /* an IPv4 address and an IPv6 address with the same leading bytes are different keys */
    auth_throttle_t u;
    memset(&u, 0, sizeof u);
    const unsigned char v4[4] = {0xfe, 0x80, 0, 0};
    for (int i = 0; i <= AUTH_THROTTLE_FREE_FAILURES; i++) auth_throttle_record_failure(&u, v4, 4, now);
    assert(auth_throttle_is_locked(&u, v4, 4, now));
    assert(!auth_throttle_is_locked(&u, host_v6, 16, now));

    /* the table is bounded: far more addresses than slots, and it still works */
    auth_throttle_t full;
    memset(&full, 0, sizeof full);
    for (int i = 0; i < 4 * AUTH_THROTTLE_SLOTS; i++) {
        unsigned char key[4] = {10, 0, (unsigned char) (i / 256), (unsigned char) (i % 256)};
        for (int j = 0; j <= AUTH_THROTTLE_FREE_FAILURES; j++) {
            auth_throttle_record_failure(&full, key, 4, now + (uint64_t) i);
        }
    }
    unsigned char recent[4] = {10, 0, 0, (unsigned char) (4 * AUTH_THROTTLE_SLOTS - 1)};
    assert(auth_throttle_is_locked(&full, recent, 4, now + 4 * AUTH_THROTTLE_SLOTS));

    /* bad input is ignored, not crashed on */
    assert(!auth_throttle_is_locked(NULL, host_a, 4, now));
    assert(!auth_throttle_is_locked(&t, NULL, 4, now));
    assert(auth_throttle_record_failure(&t, host_a, 0, now) == 0);
    assert(auth_throttle_record_failure(&t, host_a, 17, now) == 0);

    puts("auth_throttle: ok");
    return 0;
}
