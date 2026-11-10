# Detector evaluation 2026-11-10 02:36

Board `172.20.10.14`, image fix `median_stretch`, model threshold 0.3, 47 images in 17 s, median 125 ms per image.

## Own night frames (0.1 lux, IR on)

| Distance | Pose | Frames | best >= 0.3 | best >= 0.4 | best >= 0.5 | best >= 0.6 | best >= 0.7 | median best |
|---|---|---|---|---|---|---|---|---|
| 1 m | - | 6 | 3/6 (50 %) | 1/6 (17 %) | 0/6 (0 %) | 0/6 (0 %) | 0/6 (0 %) | 0.38 |
| 1 m | facing | 3 | 3/3 (100 %) | 3/3 (100 %) | 2/3 (67 %) | 1/3 (33 %) | 0/3 (0 %) | 0.54 |
| 2.5 m | - | 3 | 3/3 (100 %) | 3/3 (100 %) | 3/3 (100 %) | 2/3 (67 %) | 1/3 (33 %) | 0.69 |
| 3 m | - | 5 | 5/5 (100 %) | 4/5 (80 %) | 1/5 (20 %) | 0/5 (0 %) | 0/5 (0 %) | 0.44 |
| 4.5 m | facing | 5 | 5/5 (100 %) | 5/5 (100 %) | 5/5 (100 %) | 1/5 (20 %) | 0/5 (0 %) | 0.55 |
| 5 m | side | 5 | 5/5 (100 %) | 4/5 (80 %) | 1/5 (20 %) | 0/5 (0 %) | 0/5 (0 %) | 0.45 |

| Empty frames | Frames | any box >= 0.3 | any box >= 0.4 | any box >= 0.5 | any box >= 0.6 | any box >= 0.7 |
|---|---|---|---|---|---|---|
| scene 20261110-011023 | 5 | 5/5 (100 %) | 5/5 (100 %) | 4/5 (80 %) | 3/5 (60 %) | 0/5 (0 %) |
| scene 20261110-012033 | 5 | 5/5 (100 %) | 2/5 (40 %) | 0/5 (0 %) | 0/5 (0 %) | 0/5 (0 %) |
| scene 20261110-012102 | 5 | 5/5 (100 %) | 5/5 (100 %) | 4/5 (80 %) | 0/5 (0 %) | 0/5 (0 %) |
| scene 20261110-012125 | 5 | 3/5 (60 %) | 2/5 (40 %) | 0/5 (0 %) | 0/5 (0 %) | 0/5 (0 %) |
| **all** | 20 | 18/20 (90 %) | 14/20 (70 %) | 8/20 (40 %) | 3/20 (15 %) | 0/20 (0 %) |

The tables above count *any* box. Most high scores on dark frames are whole-frame boxes
(0,0)-(319,239), which the firmware's area filter throws away. With the firmware's filters
(box height >= 10% of the frame, area <= 80%):

| Night frames | Frames | >= 0.3 | >= 0.4 | >= 0.5 | >= 0.6 | >= 0.7 |
|---|---|---|---|---|---|---|
| person <= 4 m | 17 | 5/17 (29 %) | 2/17 (12 %) | 0/17 (0 %) | 0/17 (0 %) | 0/17 (0 %) |
| person > 4 m | 10 | 6/10 (60 %) | 3/10 (30 %) | 0/10 (0 %) | 0/10 (0 %) | 0/10 (0 %) |
| empty | 20 | 4/20 (20 %) | 2/20 (10 %) | 0/20 (0 %) | 0/20 (0 %) | 0/20 (0 %) |

**Go/no-go (night person frames at <= 4 m, filtered box >= 0.5): 0/17 = 0 % -> NO-GO** (empty frames with a filtered box >= 0.6: 0/20)

