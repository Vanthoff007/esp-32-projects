#pragma once
// Day/night switch: turns the IR LEDs (and the sensor's long-exposure "night mode") on when
// it gets dark and off when it gets light. Thresholds: menuconfig -> Platform: camera and IR.

typedef enum { NS_IR_MODE_AUTO, NS_IR_MODE_ON, NS_IR_MODE_OFF } ns_ir_mode_t;

// Auto / forced on / forced off. Starts at the menuconfig choice; a change lasts until reboot.
void ns_night_set_mode(ns_ir_mode_t mode);
ns_ir_mode_t ns_night_get_mode(void);

// Read the light sensor and switch if needed. Call every 2 s or so from any task
// (and once right after ns_night_set_mode() if the change should apply at once).
void ns_night_poll(void);

// The light reading from the last poll, in lux. -1 if the sensor isn't working.
float ns_night_lux(void);
