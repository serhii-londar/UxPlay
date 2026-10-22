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
        /* quiet failures: not enough to lock, so they can be evicted */
        for (int j = 0; j < AUTH_THROTTLE_FREE_FAILURES; j++) {
            auth_throttle_record_failure(&full, key, 4, now + (uint64_t) i);
        }
    }
    unsigned char recent[4] = {10, 0, 0, (unsigned char) (4 * AUTH_THROTTLE_SLOTS - 1)};
    for (int j = 0; j <= 1; j++) auth_throttle_record_failure(&full, recent, 4, now + 4 * AUTH_THROTTLE_SLOTS);
    assert(auth_throttle_is_locked(&full, recent, 4, now + 4 * AUTH_THROTTLE_SLOTS));

    /* a locked address is never evicted to make room (rotating source addresses
     * must not reset it) */
    auth_throttle_t ev;
    memset(&ev, 0, sizeof ev);
    unsigned char locked[4] = {172, 16, 0, 1};
    for (int j = 0; j <= AUTH_THROTTLE_FREE_FAILURES; j++) auth_throttle_record_failure(&ev, locked, 4, now);
    assert(auth_throttle_is_locked(&ev, locked, 4, now));
    for (int i = 0; i < 10 * AUTH_THROTTLE_SLOTS; i++) {
        unsigned char key[4] = {10, 1, (unsigned char) (i / 256), (unsigned char) (i % 256)};
        auth_throttle_record_failure(&ev, key, 4, now + 1);
    }
    assert(auth_throttle_is_locked(&ev, locked, 4, now + 1));
    assert(auth_throttle_remaining(&ev, locked, 4, now + 1) == AUTH_THROTTLE_BASE_LOCK - 1);

    /* when every slot is a live lockout, unknown addresses are refused until the
     * earliest one ends, so more addresses than slots do not speed the guessing up */
    auth_throttle_t sat;
    memset(&sat, 0, sizeof sat);
    for (int i = 0; i < AUTH_THROTTLE_SLOTS; i++) {
        unsigned char key[4] = {10, 2, 0, (unsigned char) i};
        for (int j = 0; j <= AUTH_THROTTLE_FREE_FAILURES; j++) auth_throttle_record_failure(&sat, key, 4, now);
    }
    unsigned char fresh[4] = {10, 3, 0, 1};
    assert(!auth_throttle_is_locked(&sat, fresh, 4, now));
    assert(auth_throttle_record_failure(&sat, fresh, 4, now) > 0); /* cannot be tracked: refused */
    assert(auth_throttle_is_locked(&sat, fresh, 4, now));
    assert(auth_throttle_remaining(&sat, fresh, 4, now) == AUTH_THROTTLE_BASE_LOCK);
    /* ... and is let through again once those lockouts have ended */
    assert(!auth_throttle_is_locked(&sat, fresh, 4, now + AUTH_THROTTLE_BASE_LOCK + 1));
    unsigned char first_locked[4] = {10, 2, 0, 0};
    assert(!auth_throttle_is_locked(&sat, first_locked, 4, now + AUTH_THROTTLE_BASE_LOCK + 1));

    /* bad input is ignored, not crashed on */
    assert(!auth_throttle_is_locked(NULL, host_a, 4, now));
    assert(!auth_throttle_is_locked(&t, NULL, 4, now));
    assert(auth_throttle_record_failure(&t, host_a, 0, now) == 0);
    assert(auth_throttle_record_failure(&t, host_a, 17, now) == 0);

    /* keys: IPv4 by the full address, IPv6 by its /64, v4-mapped IPv6 as IPv4 */
    {
        unsigned char k1[AUTH_THROTTLE_KEY_MAX], k2[AUTH_THROTTLE_KEY_MAX];
        const unsigned char a1[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1};
        const unsigned char a2[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 1, 0, 2, 0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4};
        const unsigned char other64[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 1};
        assert(auth_throttle_key(a1, 16, k1) == 8);
        assert(auth_throttle_key(a2, 16, k2) == 8);
        assert(memcmp(k1, k2, 8) == 0);                   /* same /64: one record */
        assert(auth_throttle_key(other64, 16, k2) == 8);
        assert(memcmp(k1, k2, 8) != 0);                   /* another /64: its own record */
        const unsigned char mapped[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 168, 1, 40};
        assert(auth_throttle_key(mapped, 16, k1) == 4);
        assert(memcmp(k1, host_a, 4) == 0);               /* ::ffff:192.168.1.40 is 192.168.1.40 */
        /* link-local (fe80::/10) is keyed by the full address, so one attacker on the
         * link cannot throttle its neighbours */
        const unsigned char ll1[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
        const unsigned char ll2[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 9, 9, 9, 9};
        const unsigned char ll_top[16] = {0xfe, 0xbf, 0xff, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1}; /* still fe80::/10 */
        const unsigned char not_ll[16] = {0xfe, 0xc0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};     /* fec0:: is not */
        assert(auth_throttle_key(ll1, 16, k1) == 16);
        assert(auth_throttle_key(ll2, 16, k2) == 16);
        assert(memcmp(k1, k2, 16) != 0);
        assert(auth_throttle_key(ll_top, 16, k1) == 16);
        assert(auth_throttle_key(not_ll, 16, k1) == 8);
        {
            auth_throttle_t tl;
            memset(&tl, 0, sizeof tl);
            int k1l = auth_throttle_key(ll1, 16, k1);
            int k2l = auth_throttle_key(ll2, 16, k2);
            for (int i = 0; i <= AUTH_THROTTLE_FREE_FAILURES; i++) auth_throttle_record_failure(&tl, k1, k1l, now);
            assert(auth_throttle_is_locked(&tl, k1, k1l, now));      /* the attacker */
            assert(!auth_throttle_is_locked(&tl, k2, k2l, now));     /* a neighbour on the link */
        }
        assert(auth_throttle_key(host_a, 4, k1) == 4);
        assert(auth_throttle_key(host_a, 5, k1) == 0);    /* neither IPv4 nor IPv6 */
        assert(auth_throttle_key(NULL, 4, k1) == 0);

        /* rotating addresses inside one /64 does not buy more free guesses */
        auth_throttle_t t6;
        memset(&t6, 0, sizeof t6);
        unsigned char a[16];
        memcpy(a, a1, 16);
        int locked_at = -1;
        for (int i = 0; i < 20; i++) {
            a[15] = (unsigned char) i;                    /* a new address every time */
            a[14] = (unsigned char) (i * 7);
            int kl = auth_throttle_key(a, 16, k1);
            if (auth_throttle_record_failure(&t6, k1, kl, now) > 0 && locked_at < 0) locked_at = i;
        }
        assert(locked_at == AUTH_THROTTLE_FREE_FAILURES); /* the 6th, not the 100th */
        a[15] = 99;
        int kl = auth_throttle_key(a, 16, k1);
        assert(auth_throttle_is_locked(&t6, k1, kl, now));
    }

    puts("auth_throttle: ok");
    return 0;
}
