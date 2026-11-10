# Detector evaluation 2026-11-10 02:35

Board `172.20.10.14`, image fix `stretch_gamma`, model threshold 0.3, 47 images in 18 s, median 125 ms per image.

## Own night frames (0.1 lux, IR on)

| Distance | Pose | Frames | best >= 0.3 | best >= 0.4 | best >= 0.5 | best >= 0.6 | best >= 0.7 | median best |
|---|---|---|---|---|---|---|---|---|
| 1 m | - | 6 | 4/6 (67 %) | 3/6 (50 %) | 1/6 (17 %) | 0/6 (0 %) | 0/6 (0 %) | 0.48 |
| 1 m | facing | 3 | 3/3 (100 %) | 1/3 (33 %) | 1/3 (33 %) | 1/3 (33 %) | 1/3 (33 %) | 0.40 |
| 2.5 m | - | 3 | 3/3 (100 %) | 3/3 (100 %) | 3/3 (100 %) | 3/3 (100 %) | 2/3 (67 %) | 0.82 |
| 3 m | - | 5 | 4/5 (80 %) | 4/5 (80 %) | 1/5 (20 %) | 1/5 (20 %) | 1/5 (20 %) | 0.46 |
| 4.5 m | facing | 5 | 5/5 (100 %) | 5/5 (100 %) | 4/5 (80 %) | 0/5 (0 %) | 0/5 (0 %) | 0.52 |
| 5 m | side | 5 | 5/5 (100 %) | 5/5 (100 %) | 2/5 (40 %) | 1/5 (20 %) | 1/5 (20 %) | 0.48 |

| Empty frames | Frames | any box >= 0.3 | any box >= 0.4 | any box >= 0.5 | any box >= 0.6 | any box >= 0.7 |
|---|---|---|---|---|---|---|
| scene 20261110-011023 | 5 | 5/5 (100 %) | 5/5 (100 %) | 4/5 (80 %) | 0/5 (0 %) | 0/5 (0 %) |
| scene 20261110-012033 | 5 | 4/5 (80 %) | 1/5 (20 %) | 0/5 (0 %) | 0/5 (0 %) | 0/5 (0 %) |
| scene 20261110-012102 | 5 | 5/5 (100 %) | 5/5 (100 %) | 4/5 (80 %) | 0/5 (0 %) | 0/5 (0 %) |
| scene 20261110-012125 | 5 | 4/5 (80 %) | 1/5 (20 %) | 0/5 (0 %) | 0/5 (0 %) | 0/5 (0 %) |
| **all** | 20 | 18/20 (90 %) | 12/20 (60 %) | 8/20 (40 %) | 0/20 (0 %) | 0/20 (0 %) |

The tables above count *any* box. Most high scores on dark frames are whole-frame boxes
(0,0)-(319,239), which the firmware's area filter throws away. With the firmware's filters
(box height >= 10% of the frame, area <= 80%):

| Night frames | Frames | >= 0.3 | >= 0.4 | >= 0.5 | >= 0.6 | >= 0.7 |
|---|---|---|---|---|---|---|
| person <= 4 m | 17 | 4/17 (24 %) | 1/17 (6 %) | 0/17 (0 %) | 0/17 (0 %) | 0/17 (0 %) |
| person > 4 m | 10 | 4/10 (40 %) | 1/10 (10 %) | 0/10 (0 %) | 0/10 (0 %) | 0/10 (0 %) |
| empty | 20 | 3/20 (15 %) | 1/20 (5 %) | 0/20 (0 %) | 0/20 (0 %) | 0/20 (0 %) |

**Go/no-go (night person frames at <= 4 m, filtered box >= 0.5): 0/17 = 0 % -> NO-GO** (empty frames with a filtered box >= 0.6: 0/20)

