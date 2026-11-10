// Replay a recorded per-frame log through the intrusion state machine (Day 6).
// Built by tools/host_test.py; used directly and by tools/sweep.py.
//
//   replay [options] frames.csv
//     --accept 0.6  --k 3  --n 5  --min-h 0.10  --max-area 0.80
//     --absent 3000  --cooldown 30000  --cap 10  --w 320  --h 240  --quiet
//
// Input lines (from the board, captured by tools/serial_log.py):
//   F,t_ms,n,score,x1,y1,x2,y2[,score,x1,y1,x2,y2 ...]   one line per frame, n boxes
// Other lines are ignored. Output: one "E,t_ms,score" line per alert, then a summary line
//   SUMMARY,frames=...,duration_s=...,alerts=...,reappears=...,capped=...,max_score=...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intrusion_fsm.h"

static void usage(void)
{
    fprintf(stderr, "usage: replay [--accept S] [--k K] [--n N] [--min-h F] [--max-area F]\n"
                    "              [--absent MS] [--cooldown MS] [--cap N] [--w W] [--h H] [--quiet] frames.csv\n");
    exit(2);
}

int main(int argc, char **argv)
{
    fsm_params_t p = fsm_default_params();
    int img_w = 320, img_h = 240;
    int quiet = 0;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--quiet")) { quiet = 1; continue; }
        if (a[0] == '-' && !v) usage();
        if (!strcmp(a, "--accept")) p.accept_score = (float)atof(v), i++;
        else if (!strcmp(a, "--k")) p.confirm_k = atoi(v), i++;
        else if (!strcmp(a, "--n")) p.confirm_n = atoi(v), i++;
        else if (!strcmp(a, "--min-h")) p.min_box_h = (float)atof(v), i++;
        else if (!strcmp(a, "--max-area")) p.max_box_area = (float)atof(v), i++;
        else if (!strcmp(a, "--absent")) p.absent_ms = (uint32_t)atol(v), i++;
        else if (!strcmp(a, "--cooldown")) p.cooldown_ms = (uint32_t)atol(v), i++;
        else if (!strcmp(a, "--cap")) p.max_alerts_per_hour = atoi(v), i++;
        else if (!strcmp(a, "--w")) img_w = atoi(v), i++;
        else if (!strcmp(a, "--h")) img_h = atoi(v), i++;
        else if (a[0] == '-') usage();
        else path = a;
    }
    if (!path) usage();

    FILE *in = fopen(path, "r");
    if (!in) {
        perror(path);
        return 2;
    }

    intrusion_fsm_t f;
    fsm_init(&f, &p);
    char line[2048];
    long frames = 0;
    int alerts = 0, reappears = 0, capped = 0;
    unsigned long first_t = 0, last_t = 0;
    float max_score = 0;

    while (fgets(line, sizeof(line), in)) {
        if (strncmp(line, "F,", 2) != 0) continue;
        // Read numbers left to right: t_ms, n, then n groups of (score, x1, y1, x2, y2).
        // strtod() reads one number and moves `p` past it; we then skip the comma.
        char *p_ = line + 2;
        char *end;
        double t_d = strtod(p_, &end);
        if (end == p_ || *end != ',') continue;
        unsigned long t = (unsigned long)t_d;
        p_ = end + 1;
        int n = (int)strtol(p_, &end, 10);
        if (end == p_) continue;
        p_ = end;

        int hit = 0;
        float best = 0;
        for (int b = 0; b < n; b++) {
            float v[5];
            int got = 0;
            while (got < 5 && *p_ == ',') {
                p_++;
                v[got++] = (float)strtod(p_, &end);
                if (end == p_) break;
                p_ = end;
            }
            if (got < 5) break;
            if (fsm_box_accepted(&p, v[0], (int)v[1], (int)v[2], (int)v[3], (int)v[4], img_w, img_h)) {
                hit = 1;
                if (v[0] > best) best = v[0];
            }
            if (v[0] > max_score) max_score = v[0];
        }

        if (frames == 0) first_t = t;
        last_t = t;
        frames++;
        fsm_input_t fi = {.hit = hit != 0, .score = best, .now_ms = (uint32_t)t};
        fsm_action_t act = fsm_step(&f, fi);
        if (act & ACT_ALERT) {
            alerts++;
            if (!quiet) printf("E,%lu,%.2f\n", t, best);
        }
        if (act & ACT_LOG_REAPPEAR) reappears++;
        if (act & ACT_SUPPRESSED_BY_CAP) capped++;
    }
    fclose(in);

    printf("SUMMARY,frames=%ld,duration_s=%.1f,alerts=%d,reappears=%d,capped=%d,max_score=%.2f\n",
           frames, (last_t - first_t) / 1000.0, alerts, reappears, capped, max_score);
    return 0;
}
