# Future ideas (out of the 14-day scope)

Parked here so they don't creep into the sprint (plan section 15). Nothing in this list is being built.

| Idea | Why it's parked | Notes |
|---|---|---|
| Night-trained detector (ESPDet-Pico, single "person" class on own IR frames) | Needs model training; the owner decides if and when (no training without asking) | The plan's fallback if the pre-trained `pedestrian_detect` misses people at night |
| Burst of 3 JPEGs per event (+0.5 s, +1.0 s) | Stretch goal; only after M9 passes | One encode per frame, still QVGA |
| "Still present" reminder push after 60 s | Stretch goal | Second clip: "You are still being recorded" |
| Arm / disarm via an ntfy topic or the BOOT button | Stretch goal | A second, private topic for commands |
| Quiet hours | Out of scope | `NS_AUDIO_ENABLED` menuconfig flag covers testing at night |
| Automatic deletion of old evidence | Out of scope | The heartbeat warns below 10 % free space |
| Wi-Fi reconnect back-off (1, 2, 5, 10, 30 s) | Current code reconnects at once after every drop, which works but retries fast while a router is off | `components/net/wifi.c`, `on_event()` |
| Higher-resolution evidence frame (switch to SVGA for one shot) | Changing frame size mid-run stalls the camera for several frames | Would need a second capture mode |
| OTA updates, Home Assistant, multiple cameras, video recording | Plan section 15: not this sprint | |
