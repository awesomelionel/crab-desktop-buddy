#!/usr/bin/env python3
"""Bake an avatar-core animation into a C++ keyframe table.

Source animations live in deskhog_src/grok-bot-avatar-react/*.avatar.json and
are rendered in the browser by @bible-strong/avatar-core. That package's
geometry pipeline is reimplemented here; the implementation is verified
byte-identical to the published renderer's SVG path output (see --verify).

Pipeline per eye, matching avatar-core exactly:
  1. rounded rectangle outline, width x height, corner radius min(w,h)/2
  2. rotate by the eye's own tilt angle
  3. offset to (+-spacing/2 + eye.x, eye.y) in flat "face" coordinates
  4. flatten onto a sphere of radius 120: x' = R cos(y/R) sin(x/R), y' = R sin(y/R)
     (avatar-core does this for every body type, not just spherical ones)
  5. drop (x', y') onto the body's front surface -- see SURFACES below
  6. rotate point and normal by the head orientation quaternion (Euler z*x*y)
  7. perspective project with focal length 620

Blink squashes height toward 5 units: height = 5 + (rest - 5) * blink.

Outputs, for an animation baked as NAME:
  lib/avatar_face/anim_NAME.h            keyframes + measured runtime limits
  test/test_avatar_face/vectors_NAME.h   expected values for the native test

Both outputs are committed, so building the firmware never needs this script.
deskhog_src/ is gitignored reference material, so re-running a bake requires
fetching the avatar definition again.

Usage:
  tools/bake_avatar_anim.py idle              # emit both headers
  tools/bake_avatar_anim.py idle --stats      # just print measurements
  tools/bake_avatar_anim.py idle --verify     # compare against the npm package
"""
import json
import math
import os
import struct
import sys


def f32(v):
    """Round to float32. The device stores keyframes as float, so quantising
    here keeps the generated test vectors computable from exactly the same
    inputs the firmware sees."""
    return struct.unpack("f", struct.pack("f", v))[0]

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AVATAR_DIR = os.path.join(REPO, "deskhog_src", "grok-bot-avatar-react")

# Bakeable animations. `thinking` is absent on purpose: it was baked from
# grok-bot.avatar.json, which is no longer in deskhog_src/, so its committed
# header is frozen. See lib/avatar_face/anim_thinking.h.
# `corner` and `travel` are device-side style knobs, not part of the avatar
# definition, so --verify deliberately ignores them (it compares raw upstream
# geometry). They are applied before the fit measurement, so the emitted scale
# and canvas size already account for them.
#   corner: eye corner radius as a fraction of the capsule radius. 1.0 is
#           avatar-core's rounded-rectangle; lower is squarer.
#   travel: how far the eyes roam, as a fraction of the projected motion.
#           1.0 is upstream; lower contracts each eye toward the view centre
#           without shrinking the eye itself.
#   margin: px kept clear on the limiting axis, measured before travel
#           damping — the damping adds the rest of the breathing room.
#   spread: extra centre-to-centre eye separation, in device pixels, split
#           evenly between the two eyes. Applied in the device mapping rather
#           than to the pose's `spacing`, so one number means the same
#           distance in every animation whatever its scale.
TARGETS = {
    "idle": {"file": "cubee-copy.avatar.json", "anim": "idle-two",
             "corner": 0.3, "travel": 0.62, "margin": 4, "spread": 20},
    "working": {"file": "cubee-copy.avatar.json", "anim": "thinking-two-ani",
                "corner": 0.3, "travel": 0.62, "margin": 4, "spread": 20},
    # The DONE overlay is a single ~3 s burst, but the definition's celebrate
    # loop runs 8.4 s, so only its first pose would ever be seen. Same three
    # expressions in the same order, with the holds and ease tightened so all
    # three land inside the window.
    "done": {"file": "cubee-copy.avatar.json",
             "steps": [("joyful-down-right", 600), ("curious-left", 600),
                       ("playful-right", 600)],
             "transition_ms": 300,
             "blink": {"initialDelayMs": 1200, "minIntervalMs": 1800,
                       "maxIntervalMs": 3600, "durationMs": 220},
             # travel 0.5 rather than idle/working's 0.62: joyful-down-right's
             # eyes are tall and wide, and this keeps the whole burst clear of
             # the sparkle anchors in the corners.
             "corner": 0.3, "travel": 0.5, "margin": 4, "spread": 20},
    # The definition has no sleep animation, so this one is composed here from
    # its two closed-lid expressions. Their head angles differ enough that the
    # slow cross-fade reads as breathing. `blink: None` because a sleeping
    # face does not blink, and squashing an already-flat lid would only make
    # it flicker.
    "sleep": {"file": "cubee-copy.avatar.json",
              "steps": [("drowsy-closed", 4000), ("eyes-closed", 4000)],
              "transition_ms": 1600, "blink": None,
              "corner": 0.3, "travel": 0.45, "margin": 4, "spread": 20},
}

# --- device geometry ---
SCREEN_W, SCREEN_H = 240, 135

# --- avatar-core constants ---
RADIUS = 120.0
FOCAL = 620.0
QUARTER_ARC_SAMPLES = 14
LINE_SAMPLE_SPACING = 1.5
BLINK_FLOOR = 5.0

# Interpolatable pose fields, in the order used by the C++ Pose struct.
FIELDS = [
    "head_x", "head_y", "head_z",
    "w_l", "w_r", "h_l", "h_r", "spacing",
    "x_l", "x_r", "y_l", "y_r", "angle_l", "angle_r",
    "perspective",
]


def pose_from_expression(e, quantise=True):
    p = {
        "head_x": e["head"]["x"], "head_y": e["head"]["y"], "head_z": e["head"]["z"],
        "w_l": e["eyes"]["left"]["width"], "w_r": e["eyes"]["right"]["width"],
        "h_l": e["eyes"]["left"]["height"], "h_r": e["eyes"]["right"]["height"],
        "spacing": e["eyes"]["spacing"],
        "x_l": e["eyes"]["left"]["x"], "x_r": e["eyes"]["right"]["x"],
        "y_l": e["eyes"]["left"]["y"], "y_r": e["eyes"]["right"]["y"],
        "angle_l": e["eyes"]["left"]["angle"], "angle_r": e["eyes"]["right"]["angle"],
        "perspective": e["perspective"],
    }
    return {k: (f32(v) if quantise else v) for k, v in p.items()}


def lerp_pose(a, b, t):
    # avatar-core routes angles through nearestEquivalentAngle; every value in
    # these animations is already within +-180 of its neighbour so the plain
    # lerp is exact. Asserted in stats().
    return {k: a[k] + (b[k] - a[k]) * t for k in FIELDS}


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------- quaternions
def qnorm(q):
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / n for c in q)


def qmul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return qnorm((
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
    ))


def qaxis(axis, angle):
    h = angle / 2.0
    s = math.sin(h)
    return qnorm((math.cos(h), axis[0] * s, axis[1] * s, axis[2] * s))


def head_quat(pose):
    qx = qaxis((1, 0, 0), math.radians(pose["head_x"]))
    qy = qaxis((0, 1, 0), math.radians(pose["head_y"]))
    qz = qaxis((0, 0, 1), math.radians(pose["head_z"]))
    return qmul(qmul(qz, qx), qy)


def qrot(q, p):
    w, x, y, z = q
    px, py, pz = p
    tx, ty, tz = 2 * (y * pz - z * py), 2 * (z * px - x * pz), 2 * (x * py - y * px)
    return (px + w * tx + (y * tz - z * ty),
            py + w * ty + (z * tx - x * tz),
            pz + w * tz + (x * ty - y * tx))


# --------------------------------------------------------------------- surface
# avatar-core's surfaceFrontSampleAt, restricted to the two body types the
# committed animations use. Both return the front-facing surface point for a
# flattened face coordinate, plus that point's outward normal.
def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def cube_exponent(body):
    """c() in surfaces.js: roundness 0..2 maps to a superquadric exponent.
    A roundness of 0 gives a hard box (infinite exponent, i.e. a clamped face)."""
    r = clamp(body["roundness"] or 0.0, 0.0, 2.0)
    return math.inf if body["roundness"] <= 0 else 2.0 / (0.04 + r / 2 * 0.96)


def sgnpow(v, p):
    return math.copysign(abs(v) ** p, v) if v else 0.0


def normalize(v):
    n = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / n for c in v)


def sphere_front_sample(body, x, y):
    hw, hh, hd = body["width"] / 2, body["height"] / 2, body["depth"] / 2
    rem = max(0.0, 1 - (x / (hw or 1)) ** 2 - (y / (hh or 1)) ** 2)
    z = hd * math.sqrt(rem)
    p = (x, y, z)
    return p, normalize((x / (hw * hw or 1), y / (hh * hh or 1), z / (hd * hd or 1)))


def cube_front_sample(body, x, y):
    r = cube_exponent(body)
    hw = body["width"] / 2 or 1
    hh = body["height"] / 2 or 1
    hd = body["depth"] / 2 or 1
    if not math.isfinite(r):
        p = (clamp(x, -hw, hw), clamp(y, -hh, hh), hd)
    else:
        cy = clamp(y / hh, -1.0, 1.0)
        row_half = max(0.0, 1 - abs(cy) ** r) ** (1.0 / r)
        px = clamp(x, -hw * row_half, hw * row_half)
        d = px / hw
        f = max(0.0, 1 - abs(d) ** r - abs(cy) ** r) ** (1.0 / r)
        p = (px, cy * hh, hd * f)
    n = normalize((sgnpow(p[0] / hw, r - 1) / hw,
                   sgnpow(p[1] / hh, r - 1) / hh,
                   sgnpow(p[2] / hd, r - 1) / hd))
    return p, n


SURFACES = {"sphere": sphere_front_sample, "cube": cube_front_sample}


# ------------------------------------------------------------------- outlines
def rounded_rectangle(width, height, corner=1.0):
    hw, hh = width / 2.0, height / 2.0
    r = min(hh, hw) * corner
    pts = []

    def line(a, b):
        # sqrt of the squared sum rather than hypot, matching the C++ port so
        # the sample count can never differ by a ceil() boundary.
        dx, dy = b[0] - a[0], b[1] - a[1]
        n = max(2, math.ceil(math.sqrt(dx * dx + dy * dy) / LINE_SAMPLE_SPACING))
        for i in range(n):
            t = i / n
            pts.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))

    def arc(cx, cy, start):
        for i in range(QUARTER_ARC_SAMPLES):
            a = start + (i / QUARTER_ARC_SAMPLES) * (math.pi / 2)
            pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))

    line((-hw + r, -hh), (hw - r, -hh))
    arc(hw - r, -hh + r, -math.pi / 2)
    line((hw, -hh + r), (hw, hh - r))
    arc(hw - r, hh - r, 0.0)
    line((hw - r, hh), (-hw + r, hh))
    arc(-hw + r, hh - r, math.pi / 2)
    line((-hw, hh - r), (-hw, -hh + r))
    arc(-hw + r, -hh + r, math.pi)
    return pts


def eye_outline(body, pose, side, blink=1.0, corner=1.0, travel=1.0, anchor=None):
    """Projected (viewBox) outline plus per-point normal z, for one eye.
    side: -1 left, +1 right."""
    s = "l" if side < 0 else "r"
    width = pose["w_" + s]
    height = BLINK_FLOOR + (pose["h_" + s] - BLINK_FLOOR) * blink
    cx = side * pose["spacing"] / 2.0 + pose["x_" + s]
    cy = pose["y_" + s]
    ca, sa = math.cos(math.radians(pose["angle_" + s])), math.sin(math.radians(pose["angle_" + s]))
    orient = head_quat(pose)
    persp = pose["perspective"]
    front = SURFACES[body["type"]]

    out = []
    for lx, ly in rounded_rectangle(width, height, corner):
        fx = cx + lx * ca - ly * sa
        fy = cy + lx * sa + ly * ca
        # Flatten onto the reference sphere first; avatar-core does this for
        # every body type, so RADIUS is not the body's own size.
        lon, lat = fx / RADIUS, fy / RADIUS
        sx = RADIUS * math.cos(lat) * math.sin(lon)
        sy = RADIUS * math.sin(lat)
        point, normal = front(body, sx, sy)
        px, py, pz = qrot(orient, point)
        nz = qrot(orient, normal)[2]
        den = FOCAL - pz * persp
        scale = FOCAL / 0.0001 if abs(den) < 0.0001 else FOCAL / den
        out.append((px * scale, py * scale, nz))
    if travel != 1.0:
        # Contract the eye toward the anchor without resizing it: shift every
        # point by the same vector, derived from the outline's bbox centre.
        ax, ay = anchor
        xs = [p[0] for p in out]
        ys = [p[1] for p in out]
        dx = (1.0 - travel) * ((min(xs) + max(xs)) / 2 - ax)
        dy = (1.0 - travel) * ((min(ys) + max(ys)) / 2 - ay)
        out = [(p[0] - dx, p[1] - dy, p[2]) for p in out]
    return out


# ------------------------------------------------------------------- timeline
def source_path(target):
    return os.path.join(AVATAR_DIR, TARGETS[target]["file"])


def load(target, quantise=True):
    src = source_path(target)
    if not os.path.exists(src):
        sys.exit("missing %s\n"
                 "deskhog_src/ is gitignored reference material. The generated "
                 "headers are committed, so this is only needed to re-bake."
                 % os.path.relpath(src, REPO))
    d = json.load(open(src))
    body = d["body"]["primary"]
    if body["type"] not in SURFACES:
        sys.exit("body type %r is not ported; add it to SURFACES first"
                 % body["type"])
    t = TARGETS[target]
    if "anim" in t:
        anim = d["animations"][t["anim"]]
    else:
        # Composed here rather than authored in the definition.
        anim = {
            "steps": [{"expression": name, "holdMs": hold,
                       "transitionMs": t["transition_ms"], "transition": "smooth"}
                      for name, hold in t["steps"]],
            "blink": t["blink"] or {"initialDelayMs": 0, "minIntervalMs": 0,
                                    "maxIntervalMs": 0, "durationMs": 0},
        }
    keys = [s["expression"] for s in anim["steps"]]
    poses = [pose_from_expression(d["expressions"][k], quantise) for k in keys]
    return d, body, anim, keys, poses


def timeline_samples(anim, poses, neutral, transition_step_ms=10, hold_step_ms=50):
    """(t_ms, pose) over one loop. Step 0 blends in from neutral exactly as
    avatar-core does on first play; subsequent loops blend from the previous
    step, which the C++ port reproduces cyclically."""
    out = []
    t = 0
    prev = neutral
    for i, s in enumerate(anim["steps"]):
        target = poses[i]
        for k in range(0, s["transitionMs"], transition_step_ms):
            out.append((t + k, lerp_pose(prev, target, smoothstep(k / s["transitionMs"]))))
        t += s["transitionMs"]
        for k in range(0, s["holdMs"], hold_step_ms):
            out.append((t + k, target))
        t += s["holdMs"]
        prev = target
    # Second loop: step 0 blends from the last step rather than from neutral.
    s0 = anim["steps"][0]
    for k in range(0, s0["transitionMs"], transition_step_ms):
        out.append((t + k, lerp_pose(prev, poses[0], smoothstep(k / s0["transitionMs"]))))
    return out


# ---------------------------------------------------------------------- stats
def measure(body, samples, corner, travel, anchor):
    """Projected extent over the whole loop, plus the worst outline size."""
    xs, ys, max_pts, hidden = [], [], 0, 0
    # Blink squashes height, so it can only shrink the outline; sample a few
    # values anyway to confirm and to size the point buffer.
    for _, pose in samples:
        for blink in (1.0, 0.5, 0.0):
            for side in (-1, 1):
                pts = eye_outline(body, pose, side, blink, corner, travel, anchor)
                max_pts = max(max_pts, len(pts))
                if sum(p[2] for p in pts) <= 0:
                    hidden += 1
                    continue
                xs += [p[0] for p in pts]
                ys += [p[1] for p in pts]
    return (min(xs), max(xs), min(ys), max(ys)), max_pts, hidden


def to_screen(st, pts, side):
    """Projected points -> device pixels, exactly as the C++ eyeOutline does."""
    half = st["spread"] / 2.0
    return ([SCREEN_W / 2 + st["scale"] * (p[0] - st["vcx"]) + side * half
             for p in pts],
            [SCREEN_H / 2 + st["scale"] * (p[1] - st["vcy"]) for p in pts])


def stats(target):
    d, body, anim, keys, poses = load(target)
    t = TARGETS[target]
    corner, travel, margin = t["corner"], t["travel"], t["margin"]
    spread = t["spread"]
    neutral = pose_from_expression(d["expressions"]["neutral"])
    samples = timeline_samples(anim, poses, neutral)

    for p in samples:
        for f in ("head_x", "head_y", "head_z", "angle_l", "angle_r"):
            assert abs(p[1][f]) <= 180.0, "angle outside plain-lerp range"

    # Fit on the undamped extents, so the corner and travel knobs never change
    # how big the eyes are drawn: `scale` stays whatever the raw animation
    # needs to fill the panel. Travel damping then contracts everything toward
    # the same centre, and the breathing room around the loop is what falls
    # out of that contraction.
    (u0, u1, v0, v1), _, _ = measure(body, samples, corner, 1.0, (0.0, 0.0))
    scale = min((SCREEN_W - 2 * margin) / (u1 - u0),
                (SCREEN_H - 2 * margin) / (v1 - v0))
    anchor = ((u0 + u1) / 2, (v0 + v1) / 2)
    vcx, vcy = anchor

    (x0, x1, y0, y1), max_pts, hidden = measure(body, samples, corner, travel,
                                                anchor)

    partial = {"scale": scale, "vcx": vcx, "vcy": vcy, "spread": spread}

    # Largest single-eye bounding box in device pixels, for the blit canvas,
    # and the on-screen extent once the spread has pushed the eyes apart.
    bw = bh = 0
    sx0, sx1, sy0, sy1 = 1e9, -1e9, 1e9, -1e9
    for _, pose in samples:
        for blink in (1.0, 0.5, 0.0):
            for side in (-1, 1):
                pts = eye_outline(body, pose, side, blink, corner, travel, anchor)
                if sum(p[2] for p in pts) <= 0:
                    continue
                sx, sy = to_screen(partial, pts, side)
                bw = max(bw, max(sx) - min(sx))
                bh = max(bh, max(sy) - min(sy))
                sx0, sx1 = min(sx0, min(sx)), max(sx1, max(sx))
                sy0, sy1 = min(sy0, min(sy)), max(sy1, max(sy))

    assert 0 <= sx0 and sx1 <= SCREEN_W - 1, "spread pushes the eyes off screen"
    assert 0 <= sy0 and sy1 <= SCREEN_H - 1, "animation runs off screen"

    return {
        "target": target, "body": body,
        "corner": corner, "travel": travel, "margin": margin, "anchor": anchor,
        "spread": spread, "screen": (sx0, sx1, sy0, sy1),
        "keys": keys, "poses": poses, "anim": anim, "samples": samples,
        "extent": (x0, x1, y0, y1), "scale": scale, "vcx": vcx, "vcy": vcy,
        "max_pts": max_pts, "hidden": hidden,
        "canvas_w": math.ceil(bw) + 3, "canvas_h": math.ceil(bh) + 3,
        "neutral": neutral,
    }


def print_stats(st):
    x0, x1, y0, y1 = st["extent"]
    b = st["body"]
    print("body             %s %gx%gx%g roundness %g%s"
          % (b["type"], b["width"], b["height"], b["depth"], b["roundness"],
             "" if b["type"] == "sphere"
             else "  (exponent %.6f)" % cube_exponent(b)))
    print("loop length      %d ms" % sum(s["transitionMs"] + s["holdMs"]
                                        for s in st["anim"]["steps"]))
    print("viewBox extent   x %.2f .. %.2f   y %.2f .. %.2f" % (x0, x1, y0, y1))
    print("span             %.2f x %.2f units" % (x1 - x0, y1 - y0))
    print("style            corner %.2f  travel %.2f  spread %g px"
          % (st["corner"], st["travel"], st["spread"]))
    print("scale            %.4f px/unit  (margin %d px)" % (st["scale"], st["margin"]))
    print("viewBox centre   (%.3f, %.3f)" % (st["vcx"], st["vcy"]))
    print("max outline pts  %d" % st["max_pts"])
    print("eyes facing away %d samples" % st["hidden"])
    print("max eye bbox     %d x %d px  (canvas %.1f KB)"
          % (st["canvas_w"], st["canvas_h"], st["canvas_w"] * st["canvas_h"] * 2 / 1024))
    sx0, sx1, sy0, sy1 = st["screen"]
    print("screen extent    x %.1f .. %.1f   y %.1f .. %.1f" % (sx0, sx1, sy0, sy1))


# --------------------------------------------------------------------- emitters
def banner(st, what):
    t = TARGETS[st["target"]]
    if "anim" in t:
        source = "the %r animation" % t["anim"]
    else:
        source = "a sequence composed in the generator from the expressions %s" % (
            ", ".join(repr(name) for name, _ in t["steps"]))
    return ("""// Generated by tools/bake_avatar_anim.py %s -- do not edit by hand.
//
// %s %s, sourced from
// deskhog_src/grok-bot-avatar-react/%s. The generator's geometry
// port is verified byte-identical to the upstream @bible-strong/avatar-core
// renderer.
""" % (st["target"], what, source, t["file"]))


def fmt(v):
    # 9 significant digits round-trips float32 exactly, so the firmware's
    # constants are bit-identical to the values used to compute the vectors.
    s = "%.9g" % v
    if "." not in s and "e" not in s and "E" not in s:
        s += ".0"
    return s + "f"


def pose_literal(p):
    return "{ " + ", ".join(fmt(p[f]) for f in FIELDS) + " }"


def surface_literal(body):
    kind = {"sphere": "kSurfaceSphere", "cube": "kSurfaceCube"}[body["type"]]
    exp = cube_exponent(body) if body["type"] == "cube" else 0.0
    if not math.isfinite(exp):
        # A hard box clamps to the face instead of raising to a power; the
        # runtime spells that as a non-positive exponent.
        exp = 0.0
    return "{ %s, %s, %s, %s, %s }" % (
        kind, fmt(body["width"] / 2), fmt(body["height"] / 2),
        fmt(body["depth"] / 2), fmt(exp))


def emit_data_header(st):
    a = st["anim"]
    steps = a["steps"]
    name = st["target"]
    upper = name[0].upper() + name[1:]

    # Holds vary per step; the ease-in does not, and the runtime keeps one
    # transition_ms for the whole animation.
    assert all(s["transitionMs"] == steps[0]["transitionMs"] for s in steps)
    assert all(s["transition"] == "smooth" for s in steps)
    assert all(0 < s["holdMs"] <= 0xFFFF for s in steps)

    lines = [banner(st, "Keyframes for"), "#pragma once", "",
             '#include "avatar_face.h"', "", "namespace avatar_face {", ""]

    lines += [
        "// Blended in as the animation's starting pose, matching avatar-core's",
        "// first play. Later loops blend from the previous step instead.",
        "constexpr Pose k%sNeutral = %s;" % (upper, pose_literal(st["neutral"])),
        "",
        "constexpr Pose k%sSteps[%d] = {" % (upper, len(steps)),
    ]
    for key, pose in zip(st["keys"], st["poses"]):
        lines.append("    %s,  // %s" % (pose_literal(pose), key))
    lines += ["};", ""]

    lines += [
        "constexpr uint16_t k%sHolds[%d] = { %s };" % (
            upper, len(steps), ", ".join(str(s["holdMs"]) for s in steps)),
        "",
        "// screen_x = screen_cx + scale * (proj_x - view_cx)",
        "// screen_y = screen_cy + scale * (proj_y - view_cy)",
        "// `scale` is the largest factor that keeps the whole loop on the panel",
        "// with a %d px margin." % st["margin"],
        "constexpr Anim k%s = {" % upper,
        "    %s,  // surface: %s" % (surface_literal(st["body"]), st["body"]["type"]),
        "    k%sSteps," % upper,
        "    k%sHolds," % upper,
        "    k%sNeutral," % upper,
        "    %d,      // step_count" % len(steps),
        "    %d,      // transition_ms" % steps[0]["transitionMs"],
        "    %d,     // blink_initial_delay_ms" % a["blink"]["initialDelayMs"],
        "    %d,     // blink_min_interval_ms" % a["blink"]["minIntervalMs"],
        "    %d,     // blink_max_interval_ms" % a["blink"]["maxIntervalMs"],
        "    %d,      // blink_duration_ms" % a["blink"]["durationMs"],
        "    %s,  // corner: eye corner radius / capsule radius" % fmt(st["corner"]),
        "    %s,  // travel: fraction of the projected eye motion kept" % fmt(st["travel"]),
        "    %s,  // spread: extra eye separation, device px" % fmt(st["spread"]),
        "    %s,  // scale" % fmt(st["scale"]),
        "    %s,  // view_cx" % fmt(st["vcx"]),
        "    %s,  // view_cy" % fmt(st["vcy"]),
        "    %s,  // screen_cx" % fmt(SCREEN_W / 2),
        "    %s,   // screen_cy" % fmt(SCREEN_H / 2),
        "    %d,      // canvas_w" % st["canvas_w"],
        "    %d,      // canvas_h" % st["canvas_h"],
        "};",
        "",
        "// Measured over the whole loop; the shared buffers must cover them.",
        "static_assert(%d <= kMaxOutlinePoints, \"raise kMaxOutlinePoints\");"
        % (st["max_pts"] + 8),
        "static_assert(%d <= kMaxCanvasW, \"raise kMaxCanvasW\");" % st["canvas_w"],
        "static_assert(%d <= kMaxCanvasH, \"raise kMaxCanvasH\");" % st["canvas_h"],
        "",
        "}  // namespace avatar_face",
        "",
    ]
    return "\n".join(lines)


def emit_vectors_header(st):
    """Expected values for the native test, straight from the verified port."""
    body = st["body"]
    steps = st["anim"]["steps"]
    total = loop_ms(st["anim"])
    cases = []
    # A spread of times: inside every transition, inside every hold, over a
    # step boundary, and into the second loop. Times are from animation start.
    picks = {0, 120, 250, 400, 500, 1500, total, total + 250}
    edge = 0
    for s in steps:
        picks.update({edge, edge + s["transitionMs"] // 2,
                      edge + s["transitionMs"],
                      edge + s["transitionMs"] + s["holdMs"] // 2})
        edge += s["transitionMs"] + s["holdMs"]
        picks.add(edge - 1)
    picks = sorted(picks)
    for t in picks:
        for blink in (1.0, 0.35):
            pose = pose_at(st, t)
            row = {"t": t, "blink": blink, "pose": pose, "eyes": []}
            for side in (-1, 1):
                pts = eye_outline(body, pose, side, blink, st["corner"],
                                  st["travel"], st["anchor"])
                visible = sum(p[2] for p in pts) > 0
                sx, sy = to_screen(st, pts, side)
                row["eyes"].append({
                    "n": len(pts), "visible": visible,
                    "x0": min(sx), "x1": max(sx), "y0": min(sy), "y1": max(sy),
                    "first": (sx[0], sy[0]),
                    "mid": (sx[len(sx) // 2], sy[len(sy) // 2]),
                })
            cases.append(row)

    name = st["target"]
    lines = [banner(st, "Expected geometry vectors for"),
             "#pragma once", "",
             "namespace %s_vectors {" % name, "",
             "struct EyeExpect {",
             "    int   n;",
             "    bool  visible;",
             "    float x0, x1, y0, y1;      // screen-space bounding box",
             "    float first_x, first_y;",
             "    float mid_x, mid_y;",
             "};", "",
             "struct Case {",
             "    unsigned long t_ms;",
             "    float         blink;",
             "    float         pose[15];",
             "    EyeExpect     left;",
             "    EyeExpect     right;",
             "};", "",
             "constexpr int kCaseCount = %d;" % len(cases),
             "constexpr Case kCases[kCaseCount] = {"]
    for c in cases:
        pose_vals = ", ".join(fmt(c["pose"][f]) for f in FIELDS)
        parts = []
        for e in c["eyes"]:
            parts.append("{ %d, %s, %s, %s, %s, %s, %s, %s, %s, %s }" % (
                e["n"], "true" if e["visible"] else "false",
                fmt(e["x0"]), fmt(e["x1"]), fmt(e["y0"]), fmt(e["y1"]),
                fmt(e["first"][0]), fmt(e["first"][1]),
                fmt(e["mid"][0]), fmt(e["mid"][1])))
        lines.append("    { %dUL, %s, { %s },\n      %s,\n      %s },"
                     % (c["t"], fmt(c["blink"]), pose_vals, parts[0], parts[1]))
    lines += ["};", "", "}  // namespace %s_vectors" % name, ""]
    return "\n".join(lines)


def loop_ms(anim):
    return sum(s["transitionMs"] + s["holdMs"] for s in anim["steps"])


def pose_at(st, t_ms):
    """Reference implementation of the C++ runtime's pose lookup: cyclic steps
    of differing lengths, with the neutral pose only as the first blend
    source."""
    steps = st["anim"]["steps"]
    n = len(steps)
    total = loop_ms(st["anim"])
    loop = t_ms // total
    phase = t_ms % total
    idx = 0
    for i, s in enumerate(steps):
        length = s["transitionMs"] + s["holdMs"]
        if phase < length:
            idx = i
            break
        phase -= length
    target = st["poses"][idx]
    if phase >= steps[idx]["transitionMs"]:
        return dict(target)
    if loop == 0 and idx == 0:
        source = st["neutral"]
    else:
        source = st["poses"][(idx + n - 1) % n]
    return lerp_pose(source, target, smoothstep(phase / steps[idx]["transitionMs"]))


# ---------------------------------------------------------------------- verify
def verify(target):
    """Compare the port against the published renderer via node."""
    import subprocess
    import tempfile
    work = tempfile.mkdtemp()
    open(os.path.join(work, "package.json"), "w").write(
        '{"name":"v","private":true,"type":"module",'
        '"dependencies":{"@bible-strong/avatar-core":"^0.1.0"}}')
    subprocess.run(["npm", "install", "--silent"], cwd=work, check=True)
    script = """
import { renderAvatarDefinition } from '@bible-strong/avatar-core'
import { readFileSync } from 'node:fs'
const def = JSON.parse(readFileSync(process.argv[2], 'utf8'))
for (const key of process.argv.slice(3)) {
  const s = renderAvatarDefinition(def, key)
  console.log(s.geometry.leftPath)
  console.log(s.geometry.rightPath)
}
"""
    open(os.path.join(work, "v.mjs"), "w").write(script)
    # Compare at full double precision: the browser renderer has no float32
    # quantisation step, so this isolates the geometry from the bake.
    d, body, anim, keys, poses = load(target, quantise=False)
    got = subprocess.run(["node", "v.mjs", source_path(target)] + keys, cwd=work,
                         capture_output=True, text=True, check=True).stdout.splitlines()
    ok = True
    for i, key in enumerate(keys):
        for j, side in enumerate((-1, 1)):
            # Upstream geometry only: the corner/travel style knobs are ours.
            pts = eye_outline(body, poses[i], side)
            mine = "M%.2f %.2f" % (pts[0][0], pts[0][1]) + "".join(
                "L%.2f %.2f" % (p[0], p[1]) for p in pts[1:]) + "Z"
            theirs = got[i * 2 + j]
            same = mine == theirs
            ok = ok and same
            print("%-20s %s  %s" % (key, "L" if side < 0 else "R",
                                    "identical" if same else "MISMATCH"))
    print("\n%s" % ("all paths identical to @bible-strong/avatar-core"
                    if ok else "MISMATCH -- port is wrong"))
    return 0 if ok else 1


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    if len(args) != 1 or args[0] not in TARGETS:
        sys.exit("usage: bake_avatar_anim.py {%s} [--stats|--verify]"
                 % "|".join(sorted(TARGETS)))
    target = args[0]

    if "--verify" in flags:
        return verify(target)
    st = stats(target)
    print_stats(st)
    if "--stats" in flags:
        return 0
    data_path = os.path.join(REPO, "lib", "avatar_face", "anim_%s.h" % target)
    os.makedirs(os.path.dirname(data_path), exist_ok=True)
    open(data_path, "w").write(emit_data_header(st))
    print("\nwrote %s" % os.path.relpath(data_path, REPO))
    vec_dir = os.path.join(REPO, "test", "test_avatar_face")
    os.makedirs(vec_dir, exist_ok=True)
    vec_path = os.path.join(vec_dir, "vectors_%s.h" % target)
    open(vec_path, "w").write(emit_vectors_header(st))
    print("wrote %s" % os.path.relpath(vec_path, REPO))
    return 0


if __name__ == "__main__":
    sys.exit(main())
