// Host unit tests for the intrusion state machine (Day 6). Built and run on the PC by
// tools/host_test.py; compiles app/intrusion/intrusion_fsm.c unchanged.
// Each test feeds a scripted sequence of frames at 7 fps (143 ms apart) and checks the result.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intrusion_fsm.h"

static int s_failed = 0;
static int s_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        s_checks++;                                                              \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            s_failed++;                                                          \
        }                                                                        \
    } while (0)

#define FRAME_MS 143   // ~7 fps

// A tiny "world": the FSM, the clock, and counters of what it asked us to do.
typedef struct {
    intrusion_fsm_t f;
    uint32_t now;
    int alerts;
    int reappears;
    int capped;
} world_t;

static void world_init(world_t *w, fsm_params_t p)
{
    memset(w, 0, sizeof(*w));
    w->now = 1000;
    fsm_init(&w->f, &p);
}

static void frame(world_t *w, int hit)
{
    fsm_input_t in = {.hit = hit != 0, .score = hit ? 0.8f : 0, .now_ms = w->now};
    fsm_action_t a = fsm_step(&w->f, in);
    if (a & ACT_ALERT) w->alerts++;
    if (a & ACT_LOG_REAPPEAR) w->reappears++;
    if (a & ACT_SUPPRESSED_BY_CAP) w->capped++;
    w->now += FRAME_MS;
}

// Repeat the same input for a duration.
static void frames_for(world_t *w, int hit, uint32_t ms)
{
    uint32_t end = w->now + ms;
    while ((int32_t)(end - w->now) > 0) frame(w, hit);
}

static void frames_pattern(world_t *w, const char *pattern)
{
    for (const char *c = pattern; *c; c++) frame(w, *c == '1');
}

static void test_single_blip(void)
{
    world_t w;
    world_init(&w, fsm_default_params());
    frames_pattern(&w, "0001000000");
    CHECK(w.alerts == 0);
    CHECK(fsm_state(&w.f) == ST_IDLE);
}

static void test_two_of_five_is_not_enough(void)
{
    world_t w;
    world_init(&w, fsm_default_params());
    frames_pattern(&w, "1000100010001");   // never 3 hits inside 5 frames
    CHECK(w.alerts == 0);
}

static void test_three_of_five_alerts_once(void)
{
    world_t w;
    world_init(&w, fsm_default_params());
    frames_pattern(&w, "10101");
    CHECK(w.alerts == 1);
    CHECK(fsm_state(&w.f) == ST_CONFIRMED);
    frame(&w, 1);
    CHECK(fsm_state(&w.f) == ST_ALERT_ACTIVE);
}

static void test_person_stays_60s_one_event(void)
{
    world_t w;
    world_init(&w, fsm_default_params());
    // Present for 60 s with a missed frame every 4th frame (a realistic detector).
    uint32_t end = w.now + 60000;
    int i = 0;
    while ((int32_t)(end - w.now) > 0) frame(&w, (i++ % 4) != 3);
    CHECK(w.alerts == 1);
    CHECK(fsm_state(&w.f) == ST_ALERT_ACTIVE);
}

static void test_leaves_then_cooldown_then_idle(void)
{
    world_t w;
    world_init(&w, fsm_default_params());
    frames_for(&w, 1, 5000);
    CHECK(w.alerts == 1);
    frames_for(&w, 0, 2500);
    CHECK(fsm_state(&w.f) == ST_ALERT_ACTIVE);   // 2.5 s gap: still counts as present
    frames_for(&w, 0, 1000);
    CHECK(fsm_state(&w.f) == ST_COOLDOWN);       // > 3 s without a hit: left
    frames_for(&w, 0, 30000);
    CHECK(fsm_state(&w.f) == ST_IDLE);
    CHECK(w.alerts == 1);
}

static void test_reenter_during_cooldown_no_new_alert(void)
{
    world_t w;
    world_init(&w, fsm_default_params());
    frames_for(&w, 1, 3000);
    frames_for(&w, 0, 5000);
    CHECK(fsm_state(&w.f) == ST_COOLDOWN);
    frames_for(&w, 1, 3000);
    CHECK(w.alerts == 1);
    CHECK(w.reappears == 1);
    CHECK(fsm_state(&w.f) == ST_ALERT_ACTIVE);
}

static void test_reenter_after_cooldown_new_alert(void)
{
    world_t w;
    world_init(&w, fsm_default_params());
    frames_for(&w, 1, 3000);
    frames_for(&w, 0, 3200 + 30000 + 500);   // leave, then wait the whole cooldown
    CHECK(fsm_state(&w.f) == ST_IDLE);
    frames_for(&w, 1, 3000);
    CHECK(w.alerts == 2);
}

static void test_blip_during_cooldown_no_reappear(void)
{
    world_t w;
    world_init(&w, fsm_default_params());
    frames_for(&w, 1, 2000);
    frames_for(&w, 0, 4000);
    frames_pattern(&w, "0100000");
    CHECK(w.reappears == 0);
    CHECK(fsm_state(&w.f) == ST_COOLDOWN);
}

static void test_hourly_cap(void)
{
    fsm_params_t p = fsm_default_params();
    p.cooldown_ms = 1000;   // short, so many events fit in an hour
    world_t w;
    world_init(&w, p);
    for (int i = 0; i < 15; i++) {
        frames_for(&w, 1, 1000);
        frames_for(&w, 0, 3200 + 1000 + 300);
    }
    CHECK(w.alerts == p.max_alerts_per_hour);
    CHECK(w.capped == 15 - p.max_alerts_per_hour);
    // An hour after the first alerts, the cap frees up again.
    frames_for(&w, 0, 3600000);
    frames_for(&w, 1, 1000);
    CHECK(w.alerts == p.max_alerts_per_hour + 1);
}

static void test_clock_wraparound(void)
{
    world_t w;
    world_init(&w, fsm_default_params());
    w.now = 0xFFFFFFFFu - 2000;   // the uint32 ms clock wraps during the event
    frames_for(&w, 1, 4000);
    CHECK(w.alerts == 1);
    frames_for(&w, 0, 3500);
    CHECK(fsm_state(&w.f) == ST_COOLDOWN);
    frames_for(&w, 0, 30500);
    CHECK(fsm_state(&w.f) == ST_IDLE);
}

static void test_k_equals_one(void)
{
    fsm_params_t p = fsm_default_params();
    p.confirm_k = 1;
    world_t w;
    world_init(&w, p);
    frame(&w, 1);
    CHECK(w.alerts == 1);
}

static void test_box_filter(void)
{
    fsm_params_t p = fsm_default_params();
    // 320x240 frame. A person at ~3 m: about 40 x 110 px.
    CHECK(fsm_box_accepted(&p, 0.75f, 100, 60, 140, 170, 320, 240));
    CHECK(!fsm_box_accepted(&p, 0.55f, 100, 60, 140, 170, 320, 240));   // score too low
    CHECK(!fsm_box_accepted(&p, 0.90f, 100, 100, 110, 115, 320, 240));  // 15 px tall: < 10 %
    CHECK(!fsm_box_accepted(&p, 0.90f, 0, 0, 319, 239, 320, 240));      // whole frame: lens
    CHECK(fsm_box_accepted(&p, 0.90f, 60, 0, 260, 239, 320, 240));      // close person: 62 % area
}

int main(void)
{
    struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        {"single-frame blip -> no event", test_single_blip},
        {"2 of 5 -> no event", test_two_of_five_is_not_enough},
        {"3 of 5 -> one event", test_three_of_five_alerts_once},
        {"person stays 60 s -> still one event", test_person_stays_60s_one_event},
        {"leaves 3 s -> cooldown -> idle", test_leaves_then_cooldown_then_idle},
        {"re-enters during cooldown -> no new event", test_reenter_during_cooldown_no_new_alert},
        {"re-enters after cooldown -> new event", test_reenter_after_cooldown_new_alert},
        {"blip during cooldown -> nothing", test_blip_during_cooldown_no_reappear},
        {"hourly cap", test_hourly_cap},
        {"millisecond clock wrap-around", test_clock_wraparound},
        {"K = 1 alerts on the first hit", test_k_equals_one},
        {"box filter: score, height, area", test_box_filter},
    };
    int n = sizeof(tests) / sizeof(tests[0]);
    for (int i = 0; i < n; i++) {
        int before = s_failed;
        tests[i].fn();
        printf("%s  %s\n", s_failed == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("\n%d tests, %d checks, %d failed\n", n, s_checks, s_failed);
    return s_failed ? 1 : 0;
}
