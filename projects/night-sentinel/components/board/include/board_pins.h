#pragma once

// DFR1154 pin map. Every GPIO used by the project lives here.
// Source: DFRobot wiki + DFR1154_Examples.

#define PIN_STATUS_LED   3
#define PIN_IR_LED       47   // IR_CTL -> EN/PWM of SY7200A boost LED driver (U2), active-high

// OV3660 camera (from DFR1154_Examples 5_3.ino). Arduino's Y2..Y9 = D0..D7.
#define PIN_CAM_PWDN     -1   // not connected
#define PIN_CAM_RESET    -1   // not connected
#define PIN_CAM_XCLK     5
#define PIN_CAM_SIOD     8    // SCCB (I2C) data
#define PIN_CAM_SIOC     9    // SCCB (I2C) clock
#define PIN_CAM_D0       16
#define PIN_CAM_D1       18
#define PIN_CAM_D2       21
#define PIN_CAM_D3       17
#define PIN_CAM_D4       14
#define PIN_CAM_D5       7
#define PIN_CAM_D6       6
#define PIN_CAM_D7       4
#define PIN_CAM_VSYNC    1
#define PIN_CAM_HREF     2
#define PIN_CAM_PCLK     15
