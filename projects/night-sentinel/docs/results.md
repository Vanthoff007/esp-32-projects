# Results

Measured numbers against the plan's targets (plan section 10). Firmware 0.9.0, params `v1-start`. Dates are when each row was measured.

## Performance and memory (Day 5, 2026-11-10)

| Metric | Target | Measured | Status |
|---|---|---|---|
| Detection loop fps, room light (IR off) | ≥ 6.0 | **7.9** (grab 0–0.8 ms, model 125 ms, rest 0.6 ms) | ✅ M4 |
| Detection loop fps, dark (IR + night mode) | ≥ 6.0 | **5.5** (180 ms exposure caps the camera) | ⚠️ below; plan allows 5–6 (3/5 confirm ≈ 0.9 s) |
| Inference (pre + model + post) | ≤ 140 ms | 125 ms on live frames, 127 ms first frame | ✅ |
| Boot to monitoring | ≤ 8 s | 0.8 s (Wi-Fi joins in the background) | ✅ |
| Core 1 load (detect task) | ~90 % | 95 % | ✅ |
| App size | ≤ 75 % of 6 MB | 3.39 MB = 57 % | ✅ |
| Free PSRAM (minimum) | ≥ 2 MB | ~7.0 MB | ✅ |
| Free internal RAM (minimum) | ≥ 40 KB | ~19–23 KB minimum (at Wi-Fi start), ~47 KB while running | ⚠️ below target, stable |
| 10-minute fps variance | < 10 % | 7.83–7.97 over the first runs; full 10-min run not done | 🟡 |

## Detection (Day 4, 2026-11-10)

Pedestrian Pico model through `POST /detect` (the same path as live frames). "Filtered" = the firmware's box filters: height ≥ 10 % of the frame, area ≤ 80 %.

| Set | Images | Result |
|---|---|---|
| Night person ≤ 4 m (0.1 lux, IR on), filtered box ≥ 0.5 | 17 | **1/17 (6 %) → no-go** |
| Night person > 4 m, filtered box ≥ 0.5 | 10 | 0/10 |
| Night empty, filtered box ≥ 0.5 | 20 | 0/20 (no false alarms) |
| Roboflow NIR CCTV, box ≥ 0.5 with IoU ≥ 0.5 (first sample) | 200 | 140/200 (70 %) |
| Roboflow NIR, full test+valid, person ≥ 60 % of frame height (≈ 1–1.5 m mount): filtered box ≥ 0.6 on the person / ≥ 0.5 | 978 | **600/978 (61 %) / 733/978 (75 %)** |
| Pre-trained YOLO11n (PC, 320 px): night 1 m / 2.5 m+ / empty | 9 / 18 / 20 | 9/9 found / 0/18 found / 0/20 false |

**Decision (10 Nov):** a dim light stays on and the camera is mounted 1–1.5 m from the door, which is the case the Roboflow rows above cover. To be confirmed with walk-ins under that light. Why IR-only fails at night, and the options, are in [notes.md, "Day 4 night go/no-go"](notes.md#day-4-night-gono-go-2026-11-10).

## Intrusion logic (Day 6, 2026-11-10)

Host unit tests (`uv run tools/host_test.py`): **12 tests, 33 checks, 0 failed**: blip, 2-of-5, 3-of-5, 60 s presence = 1 event, leave → cooldown → idle, re-entry during and after cooldown, hourly cap, clock wrap-around, K = 1, box filter.

Replay on a recorded 43 s empty-room log (344 frames): 0 alerts, highest box score 0.39.

## Alerts (Days 7–9, 2026-11-10)

| Check | Target | Measured | Status |
|---|---|---|---|
| Speaker task plays the clip without blocking detection | fps drop < 5 % | clip played in 4.1 s; fps 7.96 before and after | 🟡 audibility at 3 m needs a speaker + ears |
| Phone push (ntfy) with JPEG | 10/10, median ≤ 5 s | HTTP 200; text 5.5 s, event with JPEG 5.9 s incl. TLS handshake; both on the ntfy server with `E00002.jpg` | 🟡 phone receipt and 10/10 run need the app |
| SD evidence | 100/100 JPEGs, CSV rows = events | no card in the slot; firmware skips cleanly and retries | 🟡 needs a card |

## Still to measure (needs the owner on site)

Night field matrix (Day 12), walk-in tests (Days 10–11), 12 h soak and fault injection (Day 13): see the run sheet in `docs/next_steps.md`.
