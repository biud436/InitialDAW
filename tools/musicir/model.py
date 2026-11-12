"""Music IR 의 공통 계산: 곡 JSON 읽기, 템포 지도, 트랙 × 구간 칸의 통계와 지문.

생성기가 IR 을 쓸 때와 명령행이 지금의 JSON 을 검사할 때 같은 함수를 쓴다.
"""
import ast
import bisect
import hashlib
import json
import math
import os
import re

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(TOOLS)
LANE_RE = re.compile(r"^(cc([0-9]|[1-9][0-9]|1[01][0-9]|12[0-7])|pitchBend|aftertouch)$")
NOTE_RE = re.compile(r"^([A-Ga-g])([#b]*)(-?[0-9]+)$")
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
SHARPS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
FLATS = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]
FLAT_KEYS = {"F major", "Bb major", "Eb major", "Ab major", "Db major", "Gb major",
             "D minor", "G minor", "C minor", "F minor", "Bb minor", "Eb minor"}
EPS = 1e-6


# ---------------------------------------------------------------------------
# 음 이름
# ---------------------------------------------------------------------------
def parse_pitch(p):
    """MIDI 번호 또는 음 이름(C4 = 60)"""
    if isinstance(p, (int, float)):
        return int(p)
    m = NOTE_RE.match(str(p))
    if not m:
        raise ValueError(f"음 이름이 아니다: {p}")
    pc = PC[m.group(1).upper()] + sum(1 if c == "#" else -1 for c in m.group(2))
    return (int(m.group(3)) + 1) * 12 + pc


def spell(pitch, flats=False):
    names = FLATS if flats else SHARPS
    return f"{names[pitch % 12]}{pitch // 12 - 1}"


def uses_flats(key):
    return bool(key) and key in FLAT_KEYS


def canon(x):
    """지문을 위한 정규형: 수는 소수 6자리 실수로 (앱이 4.0 을 4 로 저장해도 같은 값)"""
    if isinstance(x, bool) or x is None or isinstance(x, str):
        return x
    if isinstance(x, (int, float)):
        return round(float(x), 6)
    if isinstance(x, dict):
        return {k: canon(v) for k, v in x.items()}
    return [canon(v) for v in x]


def short_hash(obj, n=12):
    return hashlib.sha1(json.dumps(canon(obj), sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()[:n]


def file_sha(path):
    try:
        return hashlib.sha256(open(path, "rb").read()).hexdigest()
    except OSError:
        return None


# ---------------------------------------------------------------------------
# 곡 JSON
# ---------------------------------------------------------------------------
def load_json(path):
    with open(path) as f:
        return json.load(f)


def bar_beats(data):
    num, den = data.get("timeSignature", [4, 4])
    return num * 4.0 / den


def track_lanes(track):
    """{레인 이름: 점 목록}: 트랙에 바로 놓인 레인과 automation 안의 레인"""
    lanes = {k: v for k, v in track.items() if LANE_RE.match(k) and isinstance(v, list)}
    for k, v in (track.get("automation") or {}).items():
        if LANE_RE.match(k) and isinstance(v, list):
            lanes.setdefault(k, [])
            lanes[k] = lanes[k] + v
    return lanes


def note_key(n):
    """지문에 쓰는 음표의 값 (설명용 comment 는 뺀다). 가사는 있을 때만 더한다 (가사 없는 곡의 지문은 그대로)"""
    key = [parse_pitch(n["pitch"]), round(float(n["startBeat"]), 5), round(float(n["durationBeat"]), 5), int(n["velocity"]),
           n.get("articulation", ""), int(n.get("channel", 1)), n.get("humanize", True)]
    return key + [n["lyric"]] if "lyric" in n else key


def point_key(lane, p):
    return [lane, round(float(p["beat"]), 4), round(float(p["value"]), 4), p.get("curve", "")]


def track_settings(track):
    """음표와 레인을 뺀 트랙 설정 (악기, 믹서, 효과, 센드, 신스, 플러그인, 연주 처리)"""
    return {k: v for k, v in track.items() if k not in ("notes", "automation", "comment") and not LANE_RE.match(k)}


def global_settings(data):
    return {k: v for k, v in data.items() if k not in ("tracks", "comment", "description", "$schema")}


def composition_hash(data):
    return short_hash(data, 16)


def end_beat(data):
    r = (data.get("render") or {}).get("endBeat")
    if r is not None:
        return float(r)
    last = 0.0
    for t in data.get("tracks", []):
        for n in t.get("notes", []):
            last = max(last, float(n["startBeat"]) + float(n["durationBeat"]))
    return last


# ---------------------------------------------------------------------------
# 템포 지도 (엔진의 TempoMap 과 같은 셈: 램프는 박에 대해 bpm 이 선형)
# ---------------------------------------------------------------------------
class TempoMap:
    def __init__(self, tempo):
        pts = [{"beat": 0.0, "bpm": float(tempo)}] if isinstance(tempo, (int, float)) else sorted(tempo, key=lambda p: p["beat"])
        self.segs = []
        sec = 0.0
        for i, p in enumerate(pts):
            last = i + 1 == len(pts)
            b0, bpm0 = float(p["beat"]), float(p["bpm"])
            b1 = math.inf if last else float(pts[i + 1]["beat"])
            ramp = not last and p.get("ramp", False) and pts[i + 1]["bpm"] != p["bpm"]
            bpm1 = float(pts[i + 1]["bpm"]) if ramp else bpm0
            self.segs.append((b0, b1, bpm0, bpm1, sec))
            if not last:
                sec += self._into(self.segs[-1], b1)
        self.starts = [s[0] for s in self.segs]
        self.constant = len(pts) == 1

    @staticmethod
    def _into(s, beat):
        b0, b1, bpm0, bpm1, _ = s
        x = beat - b0
        if bpm1 == bpm0:
            return x * 60.0 / bpm0
        k = (bpm1 - bpm0) / (b1 - b0)
        return 60.0 / k * math.log((bpm0 + k * x) / bpm0)

    def seconds(self, beat):
        i = max(0, bisect.bisect_right(self.starts, beat) - 1)
        s = self.segs[i]
        return s[4] + self._into(s, beat)

    def bpm_text(self):
        bpms = sorted({round(s[2], 2) for s in self.segs} | {round(s[3], 2) for s in self.segs})
        if len(bpms) == 1:
            return f"{bpms[0]:g} BPM"
        return f"{bpms[-1]:g} BPM (루바토 {bpms[0]:g}~{bpms[-1]:g})"


def clock(seconds):
    m, s = divmod(seconds, 60.0)
    return f"{int(m)}:{s:04.1f}"


# ---------------------------------------------------------------------------
# 악기의 갈래
# ---------------------------------------------------------------------------
STRINGS = {"violins", "violas", "celli", "basses", "violin-solo", "synth.strings"}
KEYS = {"grand-piano", "piano", "celeste", "dexed", "synth.piano", "synth.ep", "synth.poly-keys"}
PLUCKED = {"harp", "guitar", "synth.pluck", "synth.edm-pluck"}
MALLETS = {"glockenspiel", "vibraphone"}
WINDS = {"flute", "oboe", "clarinet", "oboe-solo", "clarinet-solo", "cor-anglais"}
BRASS = {"horn", "horns"}
DRUMS = {"percussion", "synth.drums", "synth.edm-drums", "synth.trailer-drums"}
BASS = {"synth.bass", "synth.sub", "synth.saw-bass"}


def family(instrument):
    i = instrument or ""
    if i in STRINGS or i.startswith("sso-"):
        return "strings"
    if i in KEYS:
        return "keys"
    if i in PLUCKED:
        return "plucked"
    if i.startswith("choir"):
        return "choir"
    if i in MALLETS:
        return "mallets"
    if i in WINDS:
        return "winds"
    if i in BRASS:
        return "brass"
    if i in DRUMS or i == "timpani":
        return "percussion"
    if i in BASS:
        return "bass"
    if i == "synth.noise":
        return "fx"
    return "synth"


def pitched(instrument):
    """음높이가 화음과 관계있는 악기인가 (북과 잡음은 아니다)"""
    return (instrument or "") not in DRUMS and instrument != "synth.noise"


# ---------------------------------------------------------------------------
# 구간과 화성
# ---------------------------------------------------------------------------
class Sections:
    """[(id, 시작 박, 끝 박)]. 첫 구간 앞은 _before, 마지막 구간 뒤는 _after"""

    def __init__(self, items):
        self.items = sorted(items, key=lambda s: s[1])
        self.starts = [s[1] for s in self.items]

    def at(self, beat):
        if not self.items or beat < self.items[0][1] - EPS:
            return "_before"
        i = bisect.bisect_right(self.starts, beat + EPS) - 1
        sid, b0, b1 = self.items[i]
        return sid if beat < b1 - EPS else "_after"

    def ids(self):
        return [s[0] for s in self.items]


class Harmony:
    """화음표: [{"beat", "beats", "chord", "tones", "bass"}]"""

    def __init__(self, items):
        self.items = sorted(items or [], key=lambda c: c["beat"])
        self.starts = [c["beat"] for c in self.items]

    def at(self, beat):
        if not self.items:
            return None
        i = bisect.bisect_right(self.starts, beat + EPS) - 1
        if i < 0:
            return None
        c = self.items[i]
        return c if beat < c["beat"] + c["beats"] - EPS else None

    def between(self, b0, b1):
        return [c for c in self.items if c["beat"] < b1 - EPS and c["beat"] + c["beats"] > b0 + EPS]


# ---------------------------------------------------------------------------
# 칸: 트랙 × 구간
# ---------------------------------------------------------------------------
def max_polyphony(notes):
    events = []
    for s, d in notes:
        events.append((s, 1))
        events.append((s + d - 1e-4, -1))
    best = cur = 0
    for _, k in sorted(events, key=lambda e: (e[0], e[1])):
        cur += k
        best = max(best, cur)
    return best


def cell_stats(notes, points, bars, harmony=None, is_pitched=True):
    """칸 하나의 통계와 지문. notes: JSON 음표, points: [(레인, 점)]"""
    keys = sorted(note_key(n) for n in notes)
    pkeys = sorted(point_key(lane, p) for lane, p in points)
    out = {"n": len(notes)}
    if notes:
        pitches = [k[0] for k in keys]
        vels = [k[3] for k in keys]
        out["pitch"] = [min(pitches), max(pitches)]
        out["vel"] = [min(vels), round(sum(vels) / len(vels)), max(vels)]
        out["density"] = round(len(notes) / max(bars, EPS), 1)
        out["poly"] = max_polyphony([(k[1], k[2]) for k in keys])
        if harmony is not None and is_pitched and harmony.items:
            hits = total = 0
            for k in keys:
                c = harmony.at(k[1])
                if c is None:
                    continue
                total += 1
                hits += (k[0] % 12) in set(c["tones"]) | {c["bass"]}
            if total:
                out["chordTones"] = round(hits / total, 2)
    if points:
        cc = {}
        for lane, p in points:
            lo, hi = cc.get(lane, (math.inf, -math.inf))
            cc[lane] = (min(lo, p["value"]), max(hi, p["value"]))
        out["cc"] = {k: [round(v[0], 1), round(v[1], 1)] for k, v in sorted(cc.items())}
    out["hash"] = short_hash([keys, pkeys]) if (keys or pkeys) else ""
    return out


def split_track(track, sections):
    """트랙의 음표와 레인 점을 구간마다 나눈다: {구간: (음표, [(레인, 점)])}"""
    cells = {}
    for n in track.get("notes", []):
        sid = sections.at(float(n["startBeat"]))
        cells.setdefault(sid, ([], []))[0].append(n)
    for lane, pts in track_lanes(track).items():
        for p in pts:
            sid = sections.at(float(p["beat"]))
            cells.setdefault(sid, ([], []))[1].append((lane, p))
    return cells


def analyze(data, section_items, track_ids, harmony_items=None):
    """곡 전체의 칸. section_items: [(id, b0, b1)], track_ids: {JSON 트랙 이름: 트랙 id}.
    돌려주는 값: {"tracks": {id: {"name", "settings", "cells": {구간: 통계}}}, "global", "composition"}"""
    sections = Sections(section_items)
    harmony = Harmony(harmony_items) if harmony_items else None
    bb = bar_beats(data)
    spans = {sid: (b1 - b0) / bb for sid, b0, b1 in sections.items}
    out = {}
    for t in data.get("tracks", []):
        tid = track_ids.get(t["name"], t["name"])
        cells = {}
        for sid, (notes, points) in split_track(t, sections).items():
            cells[sid] = cell_stats(notes, points, spans.get(sid, 1.0), harmony, pitched(t.get("instrument")))
        out[tid] = {"name": t["name"], "settings": short_hash(track_settings(t)), "cells": cells}
    return {"tracks": out, "global": short_hash(global_settings(data)), "composition": composition_hash(data)}


def cell_notes(track, sections, sid):
    return split_track(track, sections).get(sid, ([], []))


def motif_match(motif, notes, start, transpose, part, bb):
    """동기의 음이 트랙에 같은 자리, 같은 높이로 있는 비율과 그 창 안의 다른 음 수"""
    mnotes = motif.get("notes") or []
    lo, hi = 0.0, motif.get("beats", 0.0)
    if part:
        lo, hi = part[0] * bb, part[1] * bb
    want = [(t - lo, p + transpose) for t, d, p in mnotes if lo - EPS <= t < hi - EPS]
    if not want:
        return None, 0
    window = [(float(n["startBeat"]) - start, parse_pitch(n["pitch"])) for n in notes
              if start - 0.02 <= float(n["startBeat"]) < start + (hi - lo) - 0.02]
    found = sum(1 for t, p in want if any(abs(t - u) < 0.02 and p == q for u, q in window))
    return round(found / len(want), 2), max(0, len(window) - found)


def functions(script):
    """{함수 이름: (첫 줄, 끝 줄)} (클래스 안의 함수는 Class.name)"""
    out = {}
    try:
        tree = ast.parse(open(script).read())
    except (OSError, SyntaxError):
        return out

    def walk(node, prefix=""):
        for child in ast.iter_child_nodes(node):
            if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
                out[prefix + child.name] = (child.lineno, child.end_lineno)
                walk(child, prefix + child.name + ".<locals>.")
            elif isinstance(child, ast.ClassDef):
                walk(child, prefix + child.name + ".")
    walk(tree)
    return out
