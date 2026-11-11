#pragma once
// Intrusion state machine: turns noisy per-frame detections into one clean event.
// Pure C (plan section 8): no ESP-IDF headers, no clocks, no I/O. The caller passes the time
// in, so the same code runs on the board and in host_test/ (unit tests + log replay on the PC).
//
//   IDLE --hit--> CANDIDATE --K of last N frames--> CONFIRMED (one frame: fires the alert)
//     ^              |                                   |
//     |    no hit in last N frames                       v
//     +--------------+                             ALERT_ACTIVE  (hits keep it here)
//     |                                                  | no hit for ABSENT_MS
//     +---- COOLDOWN_MS passed ---- COOLDOWN <-----------+
//                                      | K of N again: back to ALERT_ACTIVE, logged, no new alert
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ST_IDLE, ST_CANDIDATE, ST_CONFIRMED, ST_ALERT_ACTIVE, ST_COOLDOWN } fsm_state_t;

// What the caller must do after a step. Values are bit flags.
typedef enum {
    ACT_NONE = 0,
    ACT_ALERT = 1,              // new intrusion: speak, push, save the JPEG
    ACT_LOG_REAPPEAR = 2,       // person came back during cooldown: log it only
    ACT_SUPPRESSED_BY_CAP = 4,  // would have alerted, but the hourly cap is reached
} fsm_action_t;

typedef struct {
    float accept_score;      // a box must score at least this
    float min_box_h;         // box height / frame height, minimum
    float max_box_area;      // box area / frame area, maximum
    int confirm_k;           // hits needed ...
    int confirm_n;           // ... in the last N frames (N <= 32)
    uint32_t absent_ms;
    uint32_t cooldown_ms;
    int max_alerts_per_hour; // <= 32
} fsm_params_t;

typedef struct {
    bool hit;          // at least one box passed fsm_box_accepted()
    float score;       // best accepted score (0 if no hit); only used for logging
    uint32_t now_ms;   // any millisecond clock; wrap-around is handled
} fsm_input_t;

#define FSM_MAX_ALERT_HISTORY 32

typedef struct {
    fsm_params_t p;
    fsm_state_t state;
    uint32_t history;        // bit i = hit i frames ago (bit 0 = this frame)
    int frames_seen;         // frames since the history was cleared (caps at 32)
    uint32_t last_seen_ms;   // time of the last hit
    uint32_t cooldown_start_ms;
    uint32_t alert_times[FSM_MAX_ALERT_HISTORY];  // ring buffer of recent alert times
    int alert_count;         // entries used in alert_times
    int alert_next;          // next slot to write
} intrusion_fsm_t;

// Parameters from intrusion_config.h.
fsm_params_t fsm_default_params(void);

void fsm_init(intrusion_fsm_t *f, const fsm_params_t *p);

// One frame in, at most one set of actions out.
fsm_action_t fsm_step(intrusion_fsm_t *f, fsm_input_t in);

fsm_state_t fsm_state(const intrusion_fsm_t *f);
const char *fsm_state_name(fsm_state_t s);

// Hits counted in the last confirm_n frames.
int fsm_recent_hits(const intrusion_fsm_t *f);

// Does one detector box count as a person? Applies the score, minimum-height and
// maximum-area filters. Box corners in pixels of an img_w x img_h frame.
bool fsm_box_accepted(const fsm_params_t *p, float score, int x1, int y1, int x2, int y2,
                      int img_w, int img_h);

#ifdef __cplusplus
}
#endif
