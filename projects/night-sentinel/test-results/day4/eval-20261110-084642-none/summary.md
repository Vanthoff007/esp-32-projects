# Detector evaluation 2026-11-10 08:53

Board `172.20.10.14`, image fix `none`, model threshold 0.3, 990 images in 412 s, median 124 ms per image.

## Own night frames (0.1 lux, IR on)

| Distance | Pose | Frames | best >= 0.3 | best >= 0.4 | best >= 0.5 | best >= 0.6 | best >= 0.7 | median best |
|---|---|---|---|---|---|---|---|---|

| Empty frames | Frames | any box >= 0.3 | any box >= 0.4 | any box >= 0.5 | any box >= 0.6 | any box >= 0.7 |
|---|---|---|---|---|---|---|
| **all** | 0 | - | - | - | - | - |

The tables above count *any* box. Most high scores on dark frames are whole-frame boxes
(0,0)-(319,239), which the firmware's area filter throws away. With the firmware's filters
(box height >= 10% of the frame, area <= 80%):

| Night frames | Frames | >= 0.3 | >= 0.4 | >= 0.5 | >= 0.6 | >= 0.7 |
|---|---|---|---|---|---|---|
| person <= 4 m | 0 | - | - | - | - | - |
| person > 4 m | 0 | - | - | - | - | - |
| empty | 0 | - | - | - | - | - |

**Go/no-go (night person frames at <= 4 m, filtered box >= 0.5): 0/0 = 0 % -> NO-GO** (empty frames with a filtered box >= 0.6: 0/0)

## Roboflow NIR frames (resized to 320x240)

| Threshold | Detected (filtered box) | Detected with IoU >= 0.5 |
|---|---|---|
| 0.3 | 925/990 (93 %) | |
| 0.4 | 848/990 (86 %) | |
| 0.5 | 741/990 (75 %) | 738/990 (75 %) |
| 0.6 | 608/990 (61 %) | |
| 0.7 | 411/990 (42 %) | |

By person size (tallest labelled person / frame height). Hit = a box that passes the firmware's filters, score >= 0.6, overlapping the person (IoU >= 0.3):

| Person height | Images | Hit at 0.6 (one frame) | Filtered box >= 0.5 |
|---|---|---|---|
| < 20 % (far, > 5 m) | 0 | - | - |
| 20-40 % (~3-5 m) | 0 | - | - |
| 40-60 % (~1.5-3 m) | 12 | 8/12 (67 %) | 8/12 (67 %) |
| >= 60 % (<= 1.5 m) | 978 | 600/978 (61 %) | 733/978 (75 %) |
