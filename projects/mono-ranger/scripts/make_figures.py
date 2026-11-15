"""Generate every chart and data-driven figure used in the report.

Run:  python scripts/make_figures.py      (from the project folder)
Output: assets/charts/*.svg  and  assets/charts/*.png

Every number plotted here is either
  (a) a published figure (source noted in PUBLISHED below), or
  (b) the output of an explicit error model / engineering estimate defined in this file.
Nothing is a measured result from this project's hardware yet.
"""
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "charts")
os.makedirs(OUT, exist_ok=True)

INK = "#18212f"
MUTED = "#5b6577"
GRID = "#e3e6ec"
TEAL = "#0e7c74"
ORANGE = "#c4561a"
BLUE = "#2f5fd0"
GOLD = "#b98a12"
PLUM = "#7a3f8f"

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 10,
    "axes.edgecolor": MUTED,
    "axes.labelcolor": INK,
    "axes.titlecolor": INK,
    "axes.titlesize": 11.5,
    "axes.titleweight": "bold",
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.8,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "svg.fonttype": "none",
    "legend.frameon": False,
})


def save(fig, name):
    fig.savefig(os.path.join(OUT, name + ".svg"), bbox_inches="tight")
    fig.savefig(os.path.join(OUT, name + ".png"), dpi=160, bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 1. Parameter count vs ESP32-S3 memory (published parameter counts)
# ---------------------------------------------------------------------------
def chart_params():
    rows = [  # name, params (millions), kind, source
        ("Pinhole geometry", 0.0, "geom", "no learned weights"),
        ("Bbox-feature MLP (design)", 0.005, "est", "engineering design, ~5k params"),
        ("FastDepth (pruned)", 1.3, "pub", "Wofk 2019 (approx., pruned)"),
        ("Lite-Mono-tiny", 2.2, "pub", "Zhang 2023"),
        ("Lite-Mono", 3.1, "pub", "Zhang 2023"),
        ("Monodepth2 (ResNet-18)", 14.3, "pub", "Godard 2019 / Lite-Mono table"),
        ("Depth Anything V2-S", 24.8, "pub", "DAv2 model card"),
        ("Depth Anything V2-B", 97.5, "pub", "DAv2 model card"),
        ("Depth Anything V2-L", 335.3, "pub", "DAv2 model card"),
    ]
    names = [r[0] for r in rows]
    vals = [max(r[1], 0.001) for r in rows]
    colors = [MUTED if r[2] == "geom" else (GOLD if r[2] == "est" else TEAL) for r in rows]
    fig, ax = plt.subplots(figsize=(8.2, 4.6))
    y = np.arange(len(rows))
    ax.barh(y, vals, color=colors, height=0.62)
    ax.set_xscale("log")
    ax.set_xlim(0.0008, 2000)
    ax.set_yticks(y, names)
    ax.invert_yaxis()
    ax.set_xlabel("Parameters (millions, log scale)  ≈ MB of weights at INT8")
    ax.set_title("Model size against ESP32-S3 memory")
    for v, lab, col in [(0.512, "512 KB internal SRAM", ORANGE), (8, "8 MB PSRAM", BLUE), (16, "16 MB flash (max common)", PLUM)]:
        ax.axvline(v, color=col, lw=1.4, ls="--")
        right = col == PLUM
        ax.text(v * (1.12 if right else 0.89), {ORANGE: -0.8, BLUE: -1.35, PLUM: -1.9}[col], lab, color=col, fontsize=8.2,
                ha="left" if right else "right", va="bottom")
    for i, r in enumerate(rows):
        txt = "0" if r[1] == 0 else (f"{r[1]*1000:.0f} k" if r[1] < 0.1 else f"{r[1]:g} M")
        ax.text(vals[i] * 1.15, i, txt, va="center", fontsize=8.5, color=INK)
    ax.grid(axis="y", visible=False)
    from matplotlib.patches import Patch
    ax.legend(handles=[Patch(color=TEAL, label="Published count"), Patch(color=GOLD, label="Engineering estimate"),
                       Patch(color=MUTED, label="No network")], loc="upper center", bbox_to_anchor=(0.45, -0.13), ncol=3, fontsize=8.5)
    ax.set_xlim(0.0008, 6000)
    ax.set_ylim(len(rows) - 0.5, -2.2)
    fig.text(0.0, -0.12, "Weights are only part of the memory story: activations (feature maps) usually dominate peak RAM on MCUs.",
             fontsize=8, color=MUTED)
    save(fig, "01_model_size_vs_esp32s3_memory")


# ---------------------------------------------------------------------------
# 2. Published latency figures (each on its own hardware)
# ---------------------------------------------------------------------------
def chart_latency():
    rows = [  # label, ms, kind
        ("ESP32-S3 · TFLM person detect 96×96, no ESP-NN", 2300, "pub"),
        ("ESP32-S3 · TFLM person detect 96×96, ESP-NN", 47, "pub"),
        ("ESP32-S3 · ESP-DL pedestrian detect 224×224 (end-to-end)", 125, "pub"),
        ("ESP32-S3 · espdet_pico 224×224", 126.2, "pub"),
        ("Jetson TX2 CPU · FastDepth 224×224", 37, "pub"),
        ("Jetson TX2 GPU · FastDepth 224×224", 5.6, "pub"),
        ("ESP32-S3 · pinhole + ground-plane math, 10 boxes", 0.05, "est"),
        ("ESP32-S3 · 5k-param INT8 MLP per box", 0.5, "est"),
        ("ESP32-S3 · FastDepth-class 0.37 GMAC model", 2000, "est"),
    ]
    fig, ax = plt.subplots(figsize=(8.6, 4.8))
    y = np.arange(len(rows))
    vals = [r[1] for r in rows]
    cols = [TEAL if r[2] == "pub" else GOLD for r in rows]
    ax.barh(y, vals, color=cols, height=0.62)
    # uncertainty band on the FastDepth-on-ESP32 estimate (0.7–5 s)
    ax.set_xscale("log")
    ax.set_xlim(0.02, 20000)
    ax.set_yticks(y, [r[0] for r in rows], fontsize=8.6)
    ax.invert_yaxis()
    ax.set_xlabel("Latency per frame or per call (ms, log scale)")
    ax.set_title("Inference latency: published figures vs. estimates")
    ax.axvline(1000, color=ORANGE, ls="--", lw=1.2)
    ax.text(1080, 8.75, "1 FPS", color=ORANGE, fontsize=8.5)
    ax.axvline(100, color=BLUE, ls="--", lw=1.2)
    ax.text(108, 8.75, "10 FPS", color=BLUE, fontsize=8.5)
    for i, v in enumerate(vals):
        ax.text(v * 1.2, i, f"{v:g} ms" if i != 8 else "~0.7–5 s (est.)", va="center", fontsize=8.3, color=INK)
    ax.grid(axis="y", visible=False)
    from matplotlib.patches import Patch
    ax.legend(handles=[Patch(color=TEAL, label="Published (vendor README / paper)"), Patch(color=GOLD, label="Engineering estimate, unverified")],
              loc="upper center", fontsize=8.3, bbox_to_anchor=(0.4, -0.13), ncol=2)
    save(fig, "02_latency_published_vs_estimated")


# ---------------------------------------------------------------------------
# 3-5. Error model: geometric distance estimation for a standing person
# ---------------------------------------------------------------------------
RNG = np.random.default_rng(7)


def simulate(Z, f=277.0, cy=120.0, n=4000, sig_px=2.0, H_mu=1.70, H_sd=0.09,
             hc=2.0, hc_sd=0.02, pitch_deg=10.0, pitch_sd_deg=0.5, img_h=240):
    """Monte-Carlo error model for one true distance Z (metres, along ground).

    Returns dict of estimates for height method, ground-plane method and fusion.
    Camera: pinhole, focal length f px, mounted hc metres high, tilted down pitch_deg.
    """
    H = RNG.normal(H_mu, H_sd, n)
    th = np.deg2rad(pitch_deg)
    # true image rows of feet and head (person stands upright at ground distance Z)
    a_feet = np.arctan2(hc, Z) - th            # angle below optical axis
    a_head = np.arctan2(hc - H, Z) - th
    y_feet = cy + f * np.tan(a_feet)
    y_head = cy + f * np.tan(a_head)
    yf = y_feet + RNG.normal(0, sig_px, n)
    yh = y_head + RNG.normal(0, sig_px, n)
    h_px = yf - yh
    # height method (uses class prior H_mu); approximate distance along axis -> ground
    Zh = f * H_mu / np.clip(h_px, 1, None)
    # ground-plane method with uncertain mounting height/pitch
    hc_hat = hc + RNG.normal(0, hc_sd, n)
    th_hat = th + np.deg2rad(RNG.normal(0, pitch_sd_deg, n))
    ang = th_hat + np.arctan((yf - cy) / f)
    Zg = np.where(ang > 1e-3, hc_hat / np.tan(np.clip(ang, 1e-3, None)), np.nan)
    visible = (yf < img_h) & (yh > 0)
    # inverse-variance fusion using analytic sigma per method
    s_h = np.sqrt((H_sd / H_mu) ** 2 + (sig_px / np.clip(h_px, 1, None)) ** 2) * Zh
    dang = np.sqrt((sig_px / f) ** 2 + np.deg2rad(pitch_sd_deg) ** 2)
    s_g = np.sqrt(((Zg ** 2 + hc ** 2) / hc * dang) ** 2 + (hc_sd / hc * Zg) ** 2)
    w_h, w_g = 1 / s_h ** 2, 1 / s_g ** 2
    Zf = (w_h * Zh + w_g * Zg) / (w_h + w_g)
    return dict(Zh=Zh, Zg=Zg, Zf=Zf, visible=visible.mean())


def pct_err(est, Z):
    e = np.abs(est - Z) / Z * 100
    e = e[np.isfinite(e)]
    return np.median(e), np.percentile(e, 90)


def chart_error_vs_distance():
    Zs = np.linspace(3.2, 25, 40)
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.2), sharey=True)
    for ax, (f, res) in zip(axes, [(277.0, "QVGA 320×240 (f ≈ 277 px)"), (554.0, "VGA 640×480 (f ≈ 554 px)")]):
        cy = 120.0 if f < 300 else 240.0
        img_h = 240 if f < 300 else 480
        for key, col, lab in [("Zh", TEAL, "Height prior (Z = f·H/h)"), ("Zg", ORANGE, "Ground plane (camera height + pitch)"), ("Zf", BLUE, "Fused (inverse variance)")]:
            med, p90 = [], []
            for Z in Zs:
                r = simulate(Z, f=f, cy=cy, img_h=img_h)
                m, p = pct_err(r[key], Z)
                med.append(m); p90.append(p)
            ax.plot(Zs, med, color=col, lw=2, label=lab + " · median")
            ax.fill_between(Zs, med, p90, color=col, alpha=0.12)
        ax.set_title(res)
        ax.set_xlabel("True distance to person (m)")
        ax.set_xlim(3, 25)
        ax.set_ylim(0, 30)
    axes[0].set_ylabel("Absolute distance error (%)\nline = median, band = up to 90th pct")
    axes[0].legend(fontsize=8.2, loc="upper left")
    fig.suptitle("Error model: adult standing on flat ground, camera 2.0 m high, 10° down-tilt", fontsize=11, color=INK, y=1.02)
    fig.text(0.0, -0.06, "Assumptions: adult height N(1.70 m, 0.09 m); box-edge jitter σ = 2 px; mount height σ = 2 cm; pitch σ = 0.5°. "
             "Simulated, not measured. Below ~3 m the feet leave the frame at QVGA with this mount.", fontsize=8, color=MUTED)
    save(fig, "03_error_model_vs_distance")


def chart_pred_vs_actual():
    truths = np.repeat([3.2, 4, 5, 6, 7, 8.5, 10, 12, 15, 20], 30).astype(float)
    est_h, est_f = [], []
    for Z in truths:
        r = simulate(Z, n=1)
        est_h.append(r["Zh"][0])
        est_f.append(r["Zf"][0])
    est_h, est_f = np.array(est_h), np.array(est_f)
    jit = RNG.normal(0, 0.08, truths.size)
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.3))
    ax = axes[0]
    ax.plot([0, 22], [0, 22], color=MUTED, lw=1)
    ax.fill_between([0, 22], [0, 22 * 0.9], [0, 22 * 1.1], color=GRID, alpha=0.7, label="±10% band")
    ax.scatter(truths + jit, est_h, s=10, color=TEAL, alpha=0.55, label="Height prior only")
    ax.scatter(truths + jit, est_f, s=10, color=BLUE, alpha=0.55, label="Fused height + ground plane")
    ax.set_xlim(0, 22); ax.set_ylim(0, 26)
    ax.set_xlabel("Ground-truth distance (m)"); ax.set_ylabel("Estimated distance (m)")
    ax.set_title("Predicted vs. actual (simulated test run)")
    ax.legend(fontsize=8.3, loc="upper left")
    ax = axes[1]
    bins = [(3, 6), (6, 9), (9, 13), (13, 17), (17, 21)]
    data_h, data_f, labels = [], [], []
    for lo, hi in bins:
        m = (truths >= lo) & (truths < hi)
        data_h.append((est_h[m] - truths[m]) / truths[m] * 100)
        data_f.append((est_f[m] - truths[m]) / truths[m] * 100)
        labels.append(f"{lo}–{hi} m" if hi != 21 else f"{lo}–20 m")
    pos = np.arange(len(bins))
    b1 = ax.boxplot(data_h, positions=pos - 0.18, widths=0.3, patch_artist=True, showfliers=False)
    b2 = ax.boxplot(data_f, positions=pos + 0.18, widths=0.3, patch_artist=True, showfliers=False)
    for b, c in [(b1, TEAL), (b2, BLUE)]:
        for p in b["boxes"]:
            p.set_facecolor(c); p.set_alpha(0.35); p.set_edgecolor(c)
        for k in ("medians", "whiskers", "caps"):
            for l in b[k]:
                l.set_color(c)
    ax.axhline(0, color=MUTED, lw=1)
    ax.set_xticks(pos, labels)
    ax.set_ylabel("Signed error (%)")
    ax.set_title("Error distribution by distance range")
    ax.set_ylim(-35, 35)
    fig.text(0.0, -0.05, "Template for Phase 1 results, filled with simulated data from the error model in Fig. 6. Replace with measured runs.",
             fontsize=8, color=MUTED)
    save(fig, "04_predicted_vs_actual_simulated")


def chart_error_budget():
    """Sensitivity: which error source dominates the height method at 10 m (QVGA)."""
    Z, f, H = 10.0, 277.0, 1.70
    h = f * H / Z
    contrib = {
        "Person height spread\n(σ 9 cm adult)": 0.09 / H * 100,
        "Child vs adult prior\n(1.2 m vs 1.7 m)": (1.70 / 1.20 - 1) * 100,
        "Box jitter σ 2 px": 2 / h * 100,
        "Box cuts off feet\n(10% of height)": (1 / 0.9 - 1) * 100,
        "Focal length error\n(uncalibrated, ±5%)": 5.0,
        "Sitting / crouching\n(height ≈ 0.6×)": (1 / 0.6 - 1) * 100,
    }
    fig, ax = plt.subplots(figsize=(8.2, 3.9))
    names = list(contrib)
    vals = [contrib[k] for k in names]
    cols = [TEAL, ORANGE, TEAL, ORANGE, GOLD, ORANGE]
    ax.barh(names, vals, color=cols, height=0.6)
    ax.invert_yaxis()
    for i, v in enumerate(vals):
        ax.text(v + 0.8, i, f"{v:.0f}%", va="center", fontsize=8.6, color=INK)
    ax.set_xlabel("Distance error contributed at 10 m (%)")
    ax.set_title(f"Error budget, height method, person at 10 m (box ≈ {h:.0f} px tall at QVGA)")
    ax.set_xlim(0, 80)
    ax.grid(axis="y", visible=False)
    from matplotlib.patches import Patch
    ax.legend(handles=[Patch(color=TEAL, label="Random, averages down with tracking"),
                       Patch(color=ORANGE, label="Systematic / pose, needs a second cue"),
                       Patch(color=GOLD, label="Removed by calibration")], fontsize=8.2, loc="upper center", bbox_to_anchor=(0.4, -0.16), ncol=3)
    save(fig, "05_error_budget_height_method")


# ---------------------------------------------------------------------------
# 6. Feasibility matrix (qualitative assessment from this survey)
# ---------------------------------------------------------------------------
def chart_feasibility():
    tasks = ["Object detection\n(person/car/dog)", "Geometric distance", "Small regression net\n(MLP / tiny CNN)",
             "Low-res depth map\n(≤64×48)", "Dense depth map\n(≥256×192)", "Foundation metric depth\n(DAv2, Metric3D, Depth Pro)"]
    plats = ["ESP32-S3", "Pi 4", "Pi 5", "Pi 5 + Hailo-8L", "Jetson Orin Nano"]
    # 3 = real-time, 2 = workable (≈1–5 FPS), 1 = offline / seconds per frame, 0 = not feasible
    M = np.array([
        [2, 2, 3, 3, 3],
        [3, 3, 3, 3, 3],
        [3, 3, 3, 3, 3],
        [1, 3, 3, 3, 3],
        [0, 2, 2, 3, 3],
        [0, 1, 1, 1, 2],
    ])
    lab = {0: "No", 1: "Slow\n(s/frame)", 2: "Workable\n~1–10 FPS", 3: "Yes\n(real-time)"}
    from matplotlib.colors import ListedColormap
    cmap = ListedColormap(["#f3d9cf", "#f6ecc8", "#d5ece6", "#9fd3c7"])
    fig, ax = plt.subplots(figsize=(8.6, 5.0))
    ax.imshow(M, cmap=cmap, vmin=-0.5, vmax=3.5, aspect="auto")
    ax.set_xticks(range(len(plats)), plats)
    ax.set_yticks(range(len(tasks)), tasks, fontsize=8.8)
    ax.xaxis.tick_top()
    ax.grid(False)
    for i in range(M.shape[0]):
        for j in range(M.shape[1]):
            ax.text(j, i, lab[M[i, j]], ha="center", va="center", fontsize=8, color=INK)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_xticks(np.arange(-0.5, len(plats)), minor=True)
    ax.set_yticks(np.arange(-0.5, len(tasks)), minor=True)
    ax.grid(which="minor", color="white", lw=2)
    ax.tick_params(which="minor", length=0)
    fig.text(0.0, -0.03, "Survey assessment combining published benchmarks and engineering estimates; see Section 6 for evidence per cell.",
             fontsize=8, color=MUTED)
    save(fig, "06_hardware_feasibility_matrix")


# ---------------------------------------------------------------------------
# 7. Cost vs power vs capability
# ---------------------------------------------------------------------------
def chart_cost():
    rows = [  # name, approx USD (board + camera), typical W range, capability tier label, color
        ("ESP32-S3 camera board", 15, (0.5, 1.5), "detection + geometry", TEAL),
        ("Raspberry Pi 4 (4 GB) + cam", 80, (3, 6.5), "small depth nets ~1–3 FPS", BLUE),
        ("Raspberry Pi 5 (4 GB) + cam", 85, (3.5, 8), "small depth nets, DAv2-S slow", BLUE),
        ("Pi 5 + AI Kit (Hailo-8L)", 155, (5, 10), "detection + depth accelerated", PLUM),
        ("Jetson Orin Nano Super kit", 275, (7, 25), "foundation depth models", ORANGE),
    ]
    fig, ax = plt.subplots(figsize=(8.2, 4.4))
    for name, usd, (lo, hi), cap, col in rows:
        ax.plot([usd, usd], [lo, hi], color=col, lw=6, alpha=0.55, solid_capstyle="round")
        left = name.startswith("Raspberry Pi 4")
        ypos = 2.3 if name.startswith("Raspberry Pi 5") else (lo * hi) ** 0.5
        ax.text(usd * (0.93 if left else 1.07), ypos, f"{name}\n{cap}", fontsize=8.2, va="center",
                ha="right" if left else "left", color=INK)
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlim(8, 1200); ax.set_ylim(0.3, 40)
    ax.set_xlabel("Approximate hardware cost, board + camera (USD, log)")
    ax.set_ylabel("Typical power while running vision (W, log)")
    ax.set_title("Cost and power envelope per platform")
    fig.text(0.0, -0.05, "Approximate list prices and typical power ranges (engineering estimates; Jetson range spans its 7 W–25 W power modes). "
             "Verify against current vendor pricing.", fontsize=8, color=MUTED)
    save(fig, "07_cost_vs_power")


# ---------------------------------------------------------------------------
# 8. Synthetic scene: RGB, metric depth, relative depth, object distances
# ---------------------------------------------------------------------------
def render_scene(W=320, H=240, hfov=60.0, cam_h=1.2):
    f = (W / 2) / np.tan(np.deg2rad(hfov / 2))
    u, v = np.meshgrid(np.arange(W) + 0.5, np.arange(H) + 0.5)
    d = np.stack([(u - W / 2) / f, -(v - H / 2) / f, np.ones_like(u)], -1)  # x right, y up, z fwd
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    o = np.array([0.0, cam_h, 0.0])
    t_best = np.full((H, W), np.inf)
    col = np.zeros((H, W, 3))
    # ground plane y=0
    with np.errstate(divide="ignore", invalid="ignore"):
        tg = np.where(d[..., 1] < 0, -o[1] / d[..., 1], np.inf)
    hit = np.isfinite(tg) & (tg < 60)
    t_best[hit] = tg[hit]
    p = o + d * tg[..., None]
    checker = ((np.floor(p[..., 0]) + np.floor(p[..., 2])) % 2 == 0)
    ground = np.where(checker[..., None], [0.80, 0.80, 0.76], [0.70, 0.71, 0.67])
    fade = np.clip(tg / 40, 0, 1)[..., None]
    sky = np.array([0.86, 0.90, 0.95])
    col[hit] = (ground * (1 - fade) + sky * fade)[hit]
    col[~hit] = sky
    objects = [  # name, center x, z (front face), w, h, depth, color, true_class_height
        ("Person", -1.0, 3.0, 0.5, 1.75, 0.3, (0.17, 0.37, 0.69)),
        ("Dog", 1.0, 2.2, 0.28, 0.55, 0.8, (0.66, 0.40, 0.18)),
        ("Person", 1.8, 7.5, 0.5, 1.68, 0.3, (0.48, 0.25, 0.56)),
        ("Car", -3.2, 11.0, 1.8, 1.5, 4.2, (0.77, 0.34, 0.10)),
    ]
    boxes = []
    for name, cx, z0, w, h, dep, c in objects:
        lo = np.array([cx - w / 2, 0.0, z0]); hi = np.array([cx + w / 2, h, z0 + dep])
        with np.errstate(divide="ignore", invalid="ignore"):
            t1 = (lo - o) / d; t2 = (hi - o) / d
        tmin = np.nanmax(np.minimum(t1, t2), -1); tmax = np.nanmin(np.maximum(t1, t2), -1)
        hb = (tmax >= tmin) & (tmin > 0) & (tmin < t_best)
        # simple shading by face normal
        pp = o + d * tmin[..., None]
        shade = np.where(np.abs(pp[..., 2] - z0) < 1e-3, 1.0, 0.78)
        shade = np.where(np.abs(pp[..., 1] - h) < 1e-3, 1.12, shade)
        col[hb] = np.clip(np.array(c) * shade[..., None], 0, 1)[hb]
        t_best[hb] = tmin[hb]
        ys, xs = np.nonzero(hb)
        if len(xs):
            boxes.append((name, xs.min(), ys.min(), xs.max(), ys.max(), np.hypot(cx, z0)))
    depth_z = t_best * d[..., 2]  # depth along optical axis
    depth_z[~np.isfinite(depth_z)] = np.nan
    return col, depth_z, boxes, f


def chart_scene():
    rgb, depth, boxes, f = render_scene()
    dmax = 30
    dz = np.clip(np.nan_to_num(depth, nan=dmax), 0, dmax)
    inv = 1 / np.clip(dz, 0.5, None)
    rel = (inv - inv.min()) / (inv.max() - inv.min())
    fig, axes = plt.subplots(1, 4, figsize=(17, 3.4))
    axes[0].imshow(rgb); axes[0].set_title("A · RGB input (synthetic)")
    im = axes[1].imshow(dz, cmap="viridis_r", vmin=0, vmax=dmax)
    axes[1].set_title("B · Metric depth (m)")
    cb = fig.colorbar(im, ax=axes[1], fraction=0.046, pad=0.02); cb.set_label("metres", fontsize=8)
    im2 = axes[2].imshow(rel, cmap="magma")
    axes[2].set_title("C · Relative depth")
    cb2 = fig.colorbar(im2, ax=axes[2], fraction=0.046, pad=0.02); cb2.set_label("0 = far … 1 = near", fontsize=8)
    axes[3].imshow(rgb * 0.55 + 0.45)
    axes[3].set_title("D · Object-level output")
    for name, x0, y0, x1, y1, dist in boxes:
        axes[3].add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fill=False, ec=INK, lw=1.4))
        axes[3].text(x0, y0 - 3, f"{name} {dist:.1f} m", fontsize=7.5, color="white",
                     bbox=dict(boxstyle="square,pad=0.15", fc=INK, ec="none"))
    for ax in axes:
        ax.set_xticks([]); ax.set_yticks([]); ax.grid(False)
        for s in ax.spines.values():
            s.set_visible(True); s.set_color(GRID)
    fig.text(0.0, -0.04, "Ray-cast synthetic scene (camera 1.2 m high, 60° HFOV, 320×240). B is exact ground truth; C is what relative-depth models output "
             "(order and shape, no metres); D is what this project actually needs.", fontsize=8, color=MUTED)
    save(fig, "08_rgb_dense_relative_object_level")


# ---------------------------------------------------------------------------
# 9. Test range layout (experimental setup)
# ---------------------------------------------------------------------------
def chart_test_range():
    fig, ax = plt.subplots(figsize=(9, 3.4))
    marks = [1, 2, 3, 4, 5, 7, 10, 12, 15, 20]
    ax.plot([0, 21], [0, 0], color=MUTED, lw=1)
    for m in marks:
        ax.plot([m, m], [-0.25, 0.25], color=INK, lw=1.4)
        ax.text(m, -0.55, f"{m} m", ha="center", fontsize=8.5, color=INK, bbox=dict(fc="white", ec="none", pad=1))
    for ang in (-25, 25):
        ax.plot([0, 21], [0, 21 * np.tan(np.deg2rad(ang)) * 0.04], color=TEAL, lw=1, ls="--", zorder=0)
    ax.add_patch(Rectangle((-0.6, -0.3), 0.6, 0.6, color=TEAL))
    ax.text(-0.3, 0.55, "Camera\n(tripod, height h_c,\npitch logged)", ha="center", fontsize=8, color=TEAL)
    for m, dy, lab in [(3, 0.9, "lateral offsets\n0, ±1, ±2 m"), (10, 0.9, "subject stands on mark;\nlaser-measured range"),
                       (15, -1.25, "repeat: day, dusk, IR night,\nfacing / side-on / sitting")]:
        ax.annotate(lab, (m, 0.05 if dy > 0 else -0.25), (m + (3.0 if dy < 0 else 0), dy), ha="center", fontsize=8, color=INK, bbox=dict(fc="white", ec="none", pad=1),
                    arrowprops=dict(arrowstyle="-", color=MUTED, lw=0.8))
    ax.set_xlim(-2.2, 21.5); ax.set_ylim(-1.6, 1.6)
    ax.axis("off")
    ax.set_title("Ground-truth test range (top view, not to lateral scale)")
    save(fig, "09_test_range_layout")


if __name__ == "__main__":
    chart_params()
    chart_latency()
    chart_error_vs_distance()
    chart_pred_vs_actual()
    chart_error_budget()
    chart_feasibility()
    chart_cost()
    chart_scene()
    chart_test_range()
    # print numbers quoted in the report
    for Z in (4, 6, 10, 15, 20):
        r = simulate(Z, n=20000)
        print(f"Z={Z:>2} m  height med/p90 = {pct_err(r['Zh'], Z)[0]:.1f}/{pct_err(r['Zh'], Z)[1]:.1f}%  "
              f"ground = {pct_err(r['Zg'], Z)[0]:.1f}/{pct_err(r['Zg'], Z)[1]:.1f}%  fused = {pct_err(r['Zf'], Z)[0]:.1f}/{pct_err(r['Zf'], Z)[1]:.1f}%")
    print("written to", OUT)
