/*
 * Per-address back-off for wrong AirPlay PIN / password attempts.
 *
 * A 4-digit PIN has 10,000 values. Without a limit a sender on the LAN can walk
 * all of them in minutes, because the SRP / digest check runs here and nothing
 * in front of it slows repeated pairing attempts. This table makes the walk take
 * days: the first AUTH_THROTTLE_FREE_FAILURES wrong attempts from an address cost
 * nothing, the next one locks that address out for AUTH_THROTTLE_BASE_LOCK
 * seconds, and every further wrong attempt doubles the lockout up to
 * AUTH_THROTTLE_MAX_LOCK. A correct attempt clears the address, and an address
 * that stays quiet for AUTH_THROTTLE_FORGET_AFTER seconds is forgotten.
 *
 * Addresses are keyed with auth_throttle_key(): IPv4 by the full address, IPv6 by
 * its /64 prefix. A host controls its whole /64 (and usually rotates privacy
 * addresses inside it), so keying IPv6 by the full address would give an
 * attacker 5 free guesses per address and make the limit meaningless. The cost
 * is that every IPv6 sender in one /64, including every fe80:: neighbour on a
 * link, shares one record: while someone guesses, the others on that prefix are
 * throttled too. IPv4-mapped IPv6 addresses count as the IPv4 address.
 *
 * The table is bounded (AUTH_THROTTLE_SLOTS). A locked address is never evicted
 * to make room: otherwise an attacker rotating source addresses could push a
 * locked address out and reset it. When every slot holds a locked address and a
 * new address fails, the table is saturated: addresses it does not know are
 * refused until the earliest lockout expires, so the walk cannot be sped up by
 * using more addresses than the table has slots.
 *
 * Header-only, no allocation and no dependencies, so it can be unit-tested on
 * its own (tests/test_auth_throttle.c). The caller supplies the time in seconds
 * (any monotonic clock), which keeps the tests deterministic. A zeroed
 * auth_throttle_t (calloc) is a valid empty table. Not thread-safe: the RAOP
 * handlers that use it all run on the single httpd thread.
 */
#ifndef AUTH_THROTTLE_H
#define AUTH_THROTTLE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define AUTH_THROTTLE_FREE_FAILURES 5
#define AUTH_THROTTLE_BASE_LOCK 30u
#define AUTH_THROTTLE_MAX_LOCK 600u
#define AUTH_THROTTLE_FORGET_AFTER 3600u
#define AUTH_THROTTLE_SLOTS 64
#define AUTH_THROTTLE_KEY_MAX 16 /* an IPv6 address; IPv4 uses the first 4 bytes */

typedef struct {
    bool used;
    unsigned char key[AUTH_THROTTLE_KEY_MAX];
    int keylen;
    unsigned int failures;
    uint64_t last_failure;
    uint64_t locked_until;
} auth_throttle_entry_t;

typedef struct {
    auth_throttle_entry_t entries[AUTH_THROTTLE_SLOTS];
    /* While now < saturated_until every slot is a live lockout: unknown
     * addresses are refused too (see auth_throttle_is_locked). */
    uint64_t saturated_until;
} auth_throttle_t;

/* The table key for a raw address of [addrlen] bytes (4 = IPv4, 16 = IPv6).
 * Writes at most AUTH_THROTTLE_KEY_MAX bytes to [key] and returns the key
 * length, or 0 for an address that is neither. */
static inline int
auth_throttle_key(const unsigned char *addr, int addrlen, unsigned char *key) {
    if (!addr || !key) return 0;
    if (addrlen == 4) {
        memcpy(key, addr, 4);
        return 4;
    }
    if (addrlen == 16) {
        static const unsigned char v4_mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (memcmp(addr, v4_mapped_prefix, 12) == 0) {
            memcpy(key, addr + 12, 4); /* ::ffff:a.b.c.d is the IPv4 address a.b.c.d */
            return 4;
        }
        memcpy(key, addr, 8); /* the /64 */
        return 8;
    }
    return 0;
}

static inline auth_throttle_entry_t *
auth_throttle_find(auth_throttle_t *t, const unsigned char *key, int keylen) {
    if (!t || !key || keylen <= 0 || keylen > AUTH_THROTTLE_KEY_MAX) return NULL;
    for (int i = 0; i < AUTH_THROTTLE_SLOTS; i++) {
        auth_throttle_entry_t *e = &t->entries[i];
        if (e->used && e->keylen == keylen && memcmp(e->key, key, (size_t) keylen) == 0) return e;
    }
    return NULL;
}

/* Drops [e] when it has been quiet for AUTH_THROTTLE_FORGET_AFTER; returns it otherwise. */
static inline auth_throttle_entry_t *
auth_throttle_live(auth_throttle_entry_t *e, uint64_t now) {
    if (e && now > e->last_failure && now - e->last_failure > AUTH_THROTTLE_FORGET_AFTER) {
        memset(e, 0, sizeof(*e));
        return NULL;
    }
    return e;
}

/* Seconds left on the lockout for this address; 0 when it may try. */
static inline uint32_t
auth_throttle_remaining(auth_throttle_t *t, const unsigned char *key, int keylen, uint64_t now) {
    if (!t || !key || keylen <= 0 || keylen > AUTH_THROTTLE_KEY_MAX) return 0;
    auth_throttle_entry_t *e = auth_throttle_live(auth_throttle_find(t, key, keylen), now);
    if (!e) {
        /* Unknown address: refused only while every slot is a live lockout. */
        return (t->saturated_until > now) ? (uint32_t) (t->saturated_until - now) : 0;
    }
    if (e->locked_until <= now) return 0;
    return (uint32_t) (e->locked_until - now);
}

static inline bool
auth_throttle_is_locked(auth_throttle_t *t, const unsigned char *key, int keylen, uint64_t now) {
    return auth_throttle_remaining(t, key, keylen, now) > 0;
}

/* Records a wrong attempt; returns the lockout it caused, in seconds (0 = none yet). */
static inline uint32_t
auth_throttle_record_failure(auth_throttle_t *t, const unsigned char *key, int keylen, uint64_t now) {
    if (!t || !key || keylen <= 0 || keylen > AUTH_THROTTLE_KEY_MAX) return 0;
    auth_throttle_entry_t *e = auth_throttle_live(auth_throttle_find(t, key, keylen), now);
    if (!e) {
        /* A free slot, else the quiet unlocked entry whose last failure is
         * oldest. A locked address is never evicted. */
        auth_throttle_entry_t *victim = NULL;
        uint64_t earliest_unlock = 0;
        for (int i = 0; i < AUTH_THROTTLE_SLOTS; i++) {
            auth_throttle_entry_t *c = &t->entries[i];
            if (!c->used) { victim = c; break; }
            if (c->locked_until > now) {
                if (earliest_unlock == 0 || c->locked_until < earliest_unlock) earliest_unlock = c->locked_until;
                continue;
            }
            if (!victim || c->last_failure < victim->last_failure) victim = c;
        }
        if (!victim) {
            /* Every slot is a live lockout: this address cannot be tracked, so
             * refuse unknown addresses until the earliest lockout ends. */
            t->saturated_until = earliest_unlock;
            return (uint32_t) (earliest_unlock - now);
        }
        e = victim;
        memset(e, 0, sizeof(*e));
        e->used = true;
        memcpy(e->key, key, (size_t) keylen);
        e->keylen = keylen;
    }
    if (e->failures < 0xFFFFu) e->failures++;
    e->last_failure = now;
    if (e->failures <= AUTH_THROTTLE_FREE_FAILURES) return 0;

    unsigned int over = e->failures - AUTH_THROTTLE_FREE_FAILURES - 1; /* 0 for the first lockout */
    uint64_t lock = AUTH_THROTTLE_BASE_LOCK;
    while (over > 0 && lock < AUTH_THROTTLE_MAX_LOCK) {
        lock *= 2;
        over--;
    }
    if (lock > AUTH_THROTTLE_MAX_LOCK) lock = AUTH_THROTTLE_MAX_LOCK;
    e->locked_until = now + lock;
    return (uint32_t) lock;
}

/* A correct attempt clears the address. */
static inline void
auth_throttle_record_success(auth_throttle_t *t, const unsigned char *key, int keylen) {
    auth_throttle_entry_t *e = auth_throttle_find(t, key, keylen);
    if (e) memset(e, 0, sizeof(*e));
}

#endif /* AUTH_THROTTLE_H */
