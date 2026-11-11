#pragma once
#include <stdbool.h>

void status_led_init(void);
void status_led_set(bool on);

// Blink patterns, run by a small background task (core 0).
typedef enum {
    NS_LED_IDLE,    // 100 ms flash every 2 s: running normally
    NS_LED_ACTIVE,  // solid on: something is happening (e.g. an alert)
    NS_LED_ERROR,   // 5 Hz blink: a fatal start-up error
} ns_led_mode_t;

// Start the pattern task. Safe to call more than once.
void ns_led_start(void);
void ns_led_set_mode(ns_led_mode_t mode);
