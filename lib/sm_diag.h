/*
 * Opt-in timing log for chasing audio crackle: with SCREENMIRROR_AUDIO_DIAG set (and not "0")
 * the audio and mirror threads append CSV rows to $TMPDIR/screenmirror-audio-diag/<file>, using
 * wall-clock epoch milliseconds so they line up with the app's own captures. Off otherwise.
 * Define SM_DIAG_FILE before including; each translation unit keeps its own handle.
 */
#ifndef SM_DIAG_H
#define SM_DIAG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <sys/stat.h>

static FILE *sm_diag_fp = NULL;
static int sm_diag_state = 0; /* 0 = not checked, 1 = on, -1 = off */

static inline double sm_diag_epoch_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1e6;
}

static inline double sm_diag_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1e6;
}

static inline FILE *sm_diag(void) {
    if (sm_diag_state < 0) return NULL;
    if (sm_diag_fp) return sm_diag_fp;
    const char *flag = getenv("SCREENMIRROR_AUDIO_DIAG");
    if (!flag || !strcmp(flag, "0")) {
        sm_diag_state = -1;
        return NULL;
    }
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s%sscreenmirror-audio-diag", tmp, tmp[strlen(tmp) - 1] == '/' ? "" : "/");
    mkdir(dir, 0755);
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", dir, SM_DIAG_FILE);
    sm_diag_fp = fopen(path, "w");
    if (!sm_diag_fp) {
        sm_diag_state = -1;
        return NULL;
    }
    setvbuf(sm_diag_fp, NULL, _IOLBF, 0);
    sm_diag_state = 1;
    fprintf(sm_diag_fp, "kind,epoch_ms,a,b,c\n");
    return sm_diag_fp;
}
#else
static inline double sm_diag_epoch_ms(void) { return 0; }
static inline double sm_diag_mono_ms(void) { return 0; }
static inline FILE *sm_diag(void) { return NULL; }
#endif

#endif
