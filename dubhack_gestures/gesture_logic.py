"""Gesture rules independent of camera/MediaPipe, so game logic is testable."""
from dataclasses import dataclass
from math import acos, degrees, dist, isfinite
from typing import Optional

NONE = "NONE"
MODEL_LABELS = {
    "Open_Palm": "OPEN_PALM",
    "Closed_Fist": "FIST",
    "Victory": "TWO_FINGERS",
    "Pointing_Up": "POINT",
}



def angle(a, b, c):
    """Joint angle ABC, in degrees. All coordinates must use the same units."""
    u = [x - y for x, y in zip(a, b)]
    v = [x - y for x, y in zip(c, b)]
    denom = sum(x*x for x in u)**0.5 * sum(x*x for x in v)**0.5
    if denom < 1e-12:
        return 0.0
    cosine = sum(x*y for x, y in zip(u, v)) / denom
    return degrees(acos(max(-1.0, min(1.0, cosine))))


def geometry_features(world, image=None, image_size=(1, 1)):
    """Rotation/scale-aware features; 2D distances account for image aspect ratio."""
    if len(world) != 21 or not all(len(p) == 3 and all(isfinite(v) for v in p) for p in world):
        return {}
    p = world
    width = dist(p[5], p[17])
    if width < 1e-6:
        return {}
    fingers = []
    for m, a, b, t in ((5,6,7,8),(9,10,11,12),(13,14,15,16),(17,18,19,20)):
        length = sum(dist(p[x], p[y]) for x, y in ((m,a),(a,b),(b,t)))
        reach = dist(p[m], p[t]) / max(length, 1e-8)
        wrist = dist(p[0], p[t]) / max(dist(p[0], p[m]), 1e-8)
        pip, dip = angle(p[m],p[a],p[b]), angle(p[a],p[b],p[t])
        fingers.append({"reach": reach, "wrist": wrist, "pip": pip, "dip": dip,
                        "extended": reach > 0.86 and wrist > 1.30 and pip > 140 and dip > 125,
                        "folded": wrist < 1.15 and (pip < 140 or reach < 0.80)})
    f = {"pinch_3d": dist(p[4],p[8])/width, "fingers": fingers,
         "thumb_open": dist(p[4],p[5])/width, "pinch_2d": None, "pinch_depth": None}
    if image is not None and len(image) == 21:
        w, h = image_size
        xy = [(q[0]*w, q[1]*h) for q in image]
        scale = max(dist(xy[0],xy[9]), 0.5*dist(xy[5],xy[17]), 1e-8)
        f["pinch_2d"] = dist(xy[4],xy[8])/scale
        f["pinch_depth"] = abs(image[4][2]-image[8][2])*w/scale
    return f


def classify_hand(world, model_label, model_score, score_min=0.65, pinch_max=0.30,
                  image=None, image_size=(1,1)):
    """Six-pose game rules. Model score is not a probability for geometry rules.

    Side-on pinch may have noisy 3D reconstruction, so close projected tips plus
    an index finger reaching out of the palm provide a conservative fallback.
    A fully curled fist must not become PINCH just because tips overlap in 2D.
    """
    f = geometry_features(world, image, image_size)
    ratio = f.get("pinch_3d")
    # A high-confidence fist must not be replaced by noisy reconstructed fingers
    # during a forward punch. Keep the original fist-then-push Fire definition.
    if model_label == "Closed_Fist" and isfinite(model_score) and model_score >= score_min:
        return "FIST", ratio, "model"
    if f:
        fingers = f["fingers"]
        index = fingers[0]
        strong_fist = model_label == "Closed_Fist" and model_score >= score_min
        close = ratio < pinch_max
        projected_close = (f["pinch_2d"] is not None and f["pinch_2d"] < 0.18
                           and f["pinch_depth"] < 0.35)
        index_out = index["reach"] > 0.62 and index["wrist"] > 1.15 and index["pip"] > 100
        if not strong_fist and index_out and (close or projected_close):
            return "PINCH", ratio, "geometry"
        extended = [x["extended"] for x in fingers]
        folded = [x["folded"] for x in fingers]
        if all(extended) and f["thumb_open"] > 0.50:
            return "OPEN_PALM", ratio, "geometry"
        if extended[0] and extended[1] and folded[2] and folded[3]:
            return "TWO_FINGERS", ratio, "geometry"
        if extended[0] and all(folded[1:]):
            return "POINT", ratio, "geometry"
        if all(folded) and all(x["reach"] < 0.65 for x in fingers) and f["thumb_open"] < 1.0:
            return "FIST", ratio, "geometry"
    if isfinite(model_score) and model_score >= score_min:
        label = MODEL_LABELS.get(model_label, NONE)
        return label, ratio, "model" if label != NONE else "unknown"
    return NONE, ratio, "unknown"


def choose_gesture(hands):
    """Double palm takes priority; otherwise use the largest visible hand.

    Choosing from ALL detected hands (including unknown poses) prevents a small
    background hand from silently overriding the main hand's unknown pose.
    """
    if not hands:
        return NONE, None
    if len(hands) == 2 and all(h["gesture"] == "OPEN_PALM" for h in hands):
        center = [sum(h["palm"][i] for h in hands)/2 for i in range(2)]
        return "DOUBLE_PALM", center
    primary = max(hands, key=lambda h: h["area"])
    return primary["gesture"], primary["palm"]


@dataclass
class GestureState:
    """Require a continuous hold before a new pose; emit one event per entry."""
    hold_ms: int = 180
    cooldown_ms: int = 0
    max_gap_ms: int = 400
    gesture: str = NONE
    candidate: str = NONE
    candidate_since: int = 0
    last_seen_ms: Optional[int] = None
    last_event_ms: int = -1_000_000
    event_id: int = 0

    def update(self, raw, now_ms, has_hands=True):
        if self.last_seen_ms is not None:
            if now_ms < self.last_seen_ms:
                raise ValueError("timestamps must be nondecreasing")
            if now_ms - self.last_seen_ms > self.max_gap_ms:
                self.gesture = self.candidate = NONE
                self.candidate_since = now_ms
        self.last_seen_ms = now_ms
        if not has_hands:
            self.gesture = self.candidate = NONE
            self.candidate_since = now_ms
            return self._result(False, False)
        if raw != self.candidate:
            self.candidate, self.candidate_since = raw, now_ms
        triggered = False
        if self.gesture != raw and now_ms - self.candidate_since >= self.hold_ms:
            self.gesture = raw
            if raw != NONE and now_ms - self.last_event_ms >= self.cooldown_ms:
                self.event_id += 1
                self.last_event_ms = now_ms
                triggered = True
        # Keep the stable pose through brief classification jitter. Missing hands
        # still release immediately above. The receiver applies per-action cooldown.
        confirmed = self.gesture != NONE
        return self._result(triggered, confirmed)

    def _result(self, triggered, confirmed):
        return {
            "gesture": self.gesture,
            "confirmed": confirmed, "triggered": triggered,
            "event_id": self.event_id,
        }
