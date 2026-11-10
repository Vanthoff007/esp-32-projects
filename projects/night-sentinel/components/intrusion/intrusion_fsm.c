// Intrusion state machine. See include/intrusion_fsm.h for the diagram.
// Pure C on purpose: host_test/ compiles this same file on the PC.
#include "intrusion_fsm.h"
#include "intrusion_config.h"

#include <string.h>

fsm_params_t fsm_default_params(void)
{
    fsm_params_t p = {
        .accept_score = NS_ACCEPT_SCORE,
        .min_box_h = NS_MIN_BOX_H,
        .max_box_area = NS_MAX_BOX_AREA,
        .confirm_k = NS_CONFIRM_K,
        .confirm_n = NS_CONFIRM_N,
        .absent_ms = NS_ABSENT_MS,
        .cooldown_ms = NS_COOLDOWN_MS,
        .max_alerts_per_hour = NS_MAX_ALERTS_PER_HOUR,
    };
    return p;
}

void fsm_init(intrusion_fsm_t *f, const fsm_params_t *p)
{
    memset(f, 0, sizeof(*f));   // every field to 0: state ST_IDLE, empty history
    f->p = *p;
    // Keep the values inside what the bit-history and alert ring can hold.
    if (f->p.confirm_n < 1) f->p.confirm_n = 1;
    if (f->p.confirm_n > 32) f->p.confirm_n = 32;
    if (f->p.confirm_k < 1) f->p.confirm_k = 1;
    if (f->p.confirm_k > f->p.confirm_n) f->p.confirm_k = f->p.confirm_n;
    if (f->p.max_alerts_per_hour > FSM_MAX_ALERT_HISTORY) f->p.max_alerts_per_hour = FSM_MAX_ALERT_HISTORY;
}

fsm_state_t fsm_state(const intrusion_fsm_t *f)
{
    return f->state;
}

const char *fsm_state_name(fsm_state_t s)
{
    switch (s) {
    case ST_IDLE: return "IDLE";
    case ST_CANDIDATE: return "CANDIDATE";
    case ST_CONFIRMED: return "CONFIRMED";
    case ST_ALERT_ACTIVE: return "ALERT_ACTIVE";
    case ST_COOLDOWN: return "COOLDOWN";
    }
    return "?";
}

// Count the 1-bits among the newest confirm_n entries of the history.
int fsm_recent_hits(const intrusion_fsm_t *f)
{
    uint32_t mask = f->p.confirm_n >= 32 ? 0xffffffffu : ((1u << f->p.confirm_n) - 1);
    uint32_t bits = f->history & mask;
    int n = 0;
    while (bits) {
        n += bits & 1;
        bits >>= 1;
    }
    return n;
}

// Alerts fired in the last hour. Times are compared by subtraction, which stays correct
// when the millisecond counter wraps around (every 49.7 days for a uint32_t).
static int alerts_last_hour(const intrusion_fsm_t *f, uint32_t now)
{
    int n = 0;
    for (int i = 0; i < f->alert_count; i++) {
        if ((uint32_t)(now - f->alert_times[i]) < 3600000u) {
            n++;
        }
    }
    return n;
}

static void remember_alert(intrusion_fsm_t *f, uint32_t now)
{
    f->alert_times[f->alert_next] = now;
    f->alert_next = (f->alert_next + 1) % FSM_MAX_ALERT_HISTORY;
    if (f->alert_count < FSM_MAX_ALERT_HISTORY) {
        f->alert_count++;
    }
}

fsm_action_t fsm_step(intrusion_fsm_t *f, fsm_input_t in)
{
    fsm_action_t act = ACT_NONE;

    // Shift this frame into the hit history (bit 0 = newest).
    f->history = (f->history << 1) | (in.hit ? 1u : 0u);
    if (f->frames_seen < 32) f->frames_seen++;
    if (in.hit) {
        f->last_seen_ms = in.now_ms;
    }
    int hits = fsm_recent_hits(f);
    bool confirmed = hits >= f->p.confirm_k;

    switch (f->state) {
    case ST_IDLE:
        if (in.hit) {
            f->state = confirmed ? ST_CONFIRMED : ST_CANDIDATE;   // K = 1 confirms at once
        }
        break;

    case ST_CANDIDATE:
        if (confirmed) {
            f->state = ST_CONFIRMED;
        } else if (hits == 0) {
            f->state = ST_IDLE;   // it was a blip
        }
        break;

    case ST_CONFIRMED:   // lasts exactly one frame: the alert was fired on the way in
    case ST_ALERT_ACTIVE:
        f->state = ST_ALERT_ACTIVE;
        if ((uint32_t)(in.now_ms - f->last_seen_ms) >= f->p.absent_ms) {
            f->state = ST_COOLDOWN;
            f->cooldown_start_ms = in.now_ms;
        }
        break;

    case ST_COOLDOWN:
        if (confirmed) {
            f->state = ST_ALERT_ACTIVE;   // same person back (or a new one) during cooldown
            act |= ACT_LOG_REAPPEAR;
        } else if ((uint32_t)(in.now_ms - f->cooldown_start_ms) >= f->p.cooldown_ms) {
            f->state = ST_IDLE;
            f->history = 0;   // start fresh: old hits don't count toward the next event
            f->frames_seen = 0;
        }
        break;
    }

    // Entering CONFIRMED is the single place an alert can fire.
    if (f->state == ST_CONFIRMED) {
        if (alerts_last_hour(f, in.now_ms) < f->p.max_alerts_per_hour) {
            remember_alert(f, in.now_ms);
            act |= ACT_ALERT;
        } else {
            act |= ACT_SUPPRESSED_BY_CAP;
        }
    }
    return act;
}

bool fsm_box_accepted(const fsm_params_t *p, float score, int x1, int y1, int x2, int y2,
                      int img_w, int img_h)
{
    if (score < p->accept_score || img_w <= 0 || img_h <= 0) {
        return false;
    }
    float bw = (float)(x2 - x1) / img_w;
    float bh = (float)(y2 - y1) / img_h;
    if (bh < p->min_box_h) {
        return false;   // too small: far away or a false box on texture
    }
    if (bw * bh > p->max_box_area) {
        return false;   // covers nearly the whole frame: something on the lens
    }
    return true;
}
