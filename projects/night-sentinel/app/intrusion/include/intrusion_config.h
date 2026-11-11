#pragma once
// Intrusion parameters, in one place (plan section 8). Start values from the plan; Day 11
// tuning (tools/sweep.py on recorded logs) changes them here, then they're frozen as PARAMS_V1.
// Pure C: no ESP-IDF headers, so host_test/ can build it on the PC.

#define NS_PARAMS_VERSION        "v1-start"   // bump when a value below changes

#define NS_MODEL_SCORE_THR       0.30f   // passed to the model: low, so near misses reach the log
#define NS_ACCEPT_SCORE          0.60f   // a box must score at least this to count as a hit
#define NS_MIN_BOX_H             0.10f   // box height >= 10 % of the frame height
#define NS_MAX_BOX_AREA          0.80f   // reject boxes covering > 80 % of the frame (insect/hand on lens)
#define NS_CONFIRM_K             3       // hits needed ...
#define NS_CONFIRM_N             5       // ... within the last N frames (~0.7 s at 7 fps)
#define NS_ABSENT_MS             3000    // no hit for this long = the person left
#define NS_COOLDOWN_MS           30000   // quiet time after the person left (demo build: 15000)
#define NS_DEMO_COOLDOWN_MS      15000
#define NS_MAX_ALERTS_PER_HOUR   10      // hard cap on alerts
