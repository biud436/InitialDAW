#!/usr/bin/env python3
"""곡 JSON 과 Music IR 에서 손으로 피아노 롤에 찍는 배치 명세 (Part 1~4) 를 쓴다.

위치는 `마디.박.틱` (마디와 박은 1 부터, 틱은 0 부터, --ppq 틱이 한 박), 길이는 틱, 음은 이름(MIDI 번호), C4 = 60.
선율, 대선율, 동기, 베이스 역할의 트랙은 모든 음을 적고, 다른 트랙은 같은 내용의 마디를 무늬로 묶어 한 번 적고 쓰는 마디를 적는다.
북 트랙은 16분음표 칸의 그리드로 적는다. 곡선 (CC, 피치 벤드, 페이더, 효과 매개변수) 은 점을 모두 적는다.
9절의 자체 검증은 곡 JSON 과 IR 의 화음표에서 센다.

사용법: tools/placement-spec.py <곡> [--out 경로] [--ppq 480]
  <곡> 은 songs/ 의 폴더 이름, JSON 경로 (music-ir.py 와 같다). 기본 출력은 docs/songs/<폴더>-spec.md
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from musicir import cli  # noqa: E402

NAMES_FLAT = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]
NAMES_SHARP = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
DRUM_INSTRUMENTS = ("drum-kit", "percussion", "synth.drums", "synth.edm-drums", "synth.trailer-drums", "synth.club-drums")
FULL_ROLES = {"main-melody", "counter-melody", "motif", "bass"}
LANES = ("cc1", "cc7", "cc10", "cc11", "cc21", "cc64", "cc74", "pitchBend")
LEGATO = 0.07


class Spec:
    def __init__(self, ctx, ppq):
        self.ctx, self.ppq = ctx, ppq
        self.data, self.ir = ctx.data, ctx.ir
        self.bb = float(ctx.bb)
        self.names = NAMES_FLAT if ctx.flats else NAMES_SHARP
        self.lines = []
        self.instruments = instrument_info()

    # ---- 표기 ----
    def nm(self, p):
        return f"{self.names[p % 12]}{p // 12 - 1}({p})"

    def bar_of(self, beat):
        return int(beat // self.bb + 1e-9) + 1

    def pos(self, beat):
        bar = self.bar_of(beat)
        inbar = beat - (bar - 1) * self.bb
        bt = int(inbar + 1e-9)
        tick = int(round((inbar - bt) * self.ppq))
        if tick >= self.ppq:
            bt, tick = bt + 1, tick - self.ppq
        return f"{bar}.{bt + 1}.{tick}"

    def ticks(self, d):
        return int(round(d * self.ppq))

    def w(self, text=""):
        self.lines.append(text)

    def table(self, head, rows):
        self.w("| " + " | ".join(head) + " |")
        self.w("|" + "|".join("---" for _ in head) + "|")
        for r in rows:
            self.w("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
        self.w()

    def tie(self, s, d):
        e = s + d
        line = (self.bar_of(s)) * self.bb
        if e - line > LEGATO + 1e-6:
            return f"붙임줄: {self.pos(e)} 앞까지"
        if e > line + 1e-6:
            return f"레가토: 다음 마디 첫 음과 {self.ticks(e - line)}틱 겹침"
        return ""

    # ---- 곡의 사실 ----
    def tracks(self):
        return [t for t in self.data["tracks"] if t.get("notes")]

    def ir_track(self, t):
        for x in self.ir["tracks"]:
            if x["name"] == t["name"]:
                return x
        return {"role": []}

    def roles(self, t):
        r = self.ir_track(t).get("role", [])
        return set(r if isinstance(r, list) else [r])

    def is_drum(self, t):
        inst = t["instrument"]
        return inst in DRUM_INSTRUMENTS or bool(self.instruments.get(inst, {}).get("keyNames"))

    def key_name(self, t, p):
        names = self.instruments.get(t["instrument"], {}).get("keyNames") or {}
        return names.get(str(p), self.nm(p))

    def sections(self):
        return self.ir["sections"]

    def section_of(self, beat):
        for s in self.sections():
            if s["beats"][0] - 1e-6 <= beat < s["beats"][1] - 1e-6:
                return s
        return self.sections()[-1]

    def chord_at(self, beat):
        cur = None
        for seg in self.ir.get("harmony", []):
            if seg["beat"] <= beat + 1e-6:
                cur = seg
        return cur

    def end_beat(self):
        r = self.data.get("render", {})
        if "endBeat" in r:
            return r["endBeat"]
        return max(x["startBeat"] + x["durationBeat"] for t in self.tracks() for x in t["notes"])

    def bars(self):
        return int(math.ceil(self.end_beat() / self.bb - 1e-9))

    # ---- 쓰기 ----
    def note_rows(self, notes, art=True):
        self.w("```")
        for x in sorted(notes, key=lambda x: (x["startBeat"], x["pitch"])):
            line = f"{self.pos(x['startBeat']):>9} | {self.nm(x['pitch']):>9} | {self.ticks(x['durationBeat']):>5} | {x['velocity']:>3}"
            if art and x.get("articulation"):
                line += f" | {x['articulation']}"
            tie = self.tie(x["startBeat"], x["durationBeat"])
            if tie:
                line += f" | {tie}"
            self.w(line)
        self.w("```")

    def by_section(self, notes):
        groups = OrderedDict()
        for s in self.sections():
            sel = [x for x in notes if s["beats"][0] - 1e-6 <= x["startBeat"] < s["beats"][1] - 1e-6]
            if sel:
                groups[s["id"]] = (s, sel)
        return groups

    def bar_patterns(self, t):
        """마디마다의 내용 (마디 머리에서의 위치, 음, 길이, 벨로시티, 아티큘레이션) 이 같은 마디를 묶는다"""
        per = OrderedDict()
        for x in sorted(t["notes"], key=lambda x: (x["startBeat"], x["pitch"])):
            bar = self.bar_of(x["startBeat"])
            per.setdefault(bar, []).append((round(x["startBeat"] - (bar - 1) * self.bb, 5), x["pitch"], round(x["durationBeat"], 5),
                                            x["velocity"], x.get("articulation", "")))
        groups = OrderedDict()
        for bar, content in per.items():
            groups.setdefault(tuple(content), []).append(bar)
        return groups

    def chord_rows(self, content):
        out = OrderedDict()
        for off, p, d, v, art in content:
            out.setdefault((off, d, v, art), []).append(p)
        rows = []
        for (off, d, v, art), ps in out.items():
            bt = int(off + 1e-9)
            tick = int(round((off - bt) * self.ppq))
            rows.append(f"{bt + 1}.{tick:<4} | {' '.join(self.nm(p) for p in sorted(ps)):<34} | {self.ticks(d):>5} | {v:>3}" + (f" | {art}" if art else ""))
        return rows

    def write_patterns(self, t):
        groups = self.bar_patterns(t)
        self.w(f"마디마다 같은 내용을 무늬로 묶었다 ({len(groups)} 무늬). 무늬 줄은 `박.틱 | 음 | 길이(틱) | 벨로시티 | 아티큘레이션`, 박과 틱은 마디 머리에서 센다.")
        self.w()
        for k, (content, bars) in enumerate(groups.items(), 1):
            self.w(f"{t['name']} 무늬 {k}: {spans(bars)}마디")
            self.w("```")
            for row in self.chord_rows(content):
                self.w(row)
            self.w("```")

    def drum_grid(self, t):
        steps = int(round(self.bb * 4))
        groups = OrderedDict()
        loose = []
        per = OrderedDict()
        for x in t["notes"]:
            bar = self.bar_of(x["startBeat"])
            off = (x["startBeat"] - (bar - 1) * self.bb) * 4
            if abs(off - round(off)) > 1e-3:
                loose.append(x)
                continue
            per.setdefault(bar, {}).setdefault(x["pitch"], []).append((int(round(off)), x["velocity"]))
        for bar, pat in per.items():
            key = json.dumps({str(k): sorted(v) for k, v in sorted(pat.items())})
            groups.setdefault(key, []).append(bar)
        self.w(f"16분음표 칸의 그리드. X = 벨로시티 100 이상, x = 100 아래, . = 쉼. 줄 끝에 왼쪽부터의 벨로시티. 같은 무늬의 마디를 묶었다 ({len(groups)} 무늬).")
        lens = sorted({self.ticks(x["durationBeat"]) for x in t["notes"]})
        self.w(f"음 길이 (틱): {', '.join(str(x) for x in lens[:8])}{' 등' if len(lens) > 8 else ''}. 효과음은 건반이 길이를 정한다.")
        self.w()
        head = "".join(f"{(s // 4) + 1} " if s % 4 == 0 else ". " for s in range(steps))
        for k, (key, bars) in enumerate(groups.items(), 1):
            pat = {int(kk): vv for kk, vv in json.loads(key).items()}
            self.w(f"{t['name']} 무늬 {k}: {spans(bars)}마디")
            self.w("```")
            self.w(" " * 16 + head)
            for p in sorted(pat, reverse=True):
                cells = ["."] * steps
                for s, v in sorted(pat[p]):
                    if 0 <= s < steps:
                        cells[s] = "X" if v >= 100 else "x"
                label = f"{self.key_name(t, p)}({p})"
                self.w(f"{label:<14}  " + " ".join(cells) + "   " + " ".join(str(v) for _, v in sorted(pat[p])))
            self.w("```")
        if loose:
            self.w("칸에 맞지 않는 음:")
            self.note_rows(loose, art=False)

    def lane_points(self, title, pts):
        self.w(f"### {title} ({len(pts)} 점)")
        self.w("```")
        if len(pts) > 40:
            row = []
            for p in pts:
                row.append(f"{self.pos(p['beat'])}={p['value']:g}{'' if p.get('curve', 'linear') == 'linear' else ':' + p['curve']}")
                if len(row) == 6:
                    self.w("  ".join(row))
                    row = []
            if row:
                self.w("  ".join(row))
        else:
            for p in pts:
                self.w(f"{self.pos(p['beat']):>9} | {p['value']:>8g} | {p.get('curve', 'linear')}")
        self.w("```")

    # ---- Part 1 ----
    def part1(self):
        d, song = self.data, self.ir["song"]
        tempo = d["tempo"]
        bpm = tempo[0]["bpm"]
        const = len(tempo) == 1
        ts = d.get("timeSignature", [4, 4])
        self.w("# Part 1. 설계")
        self.w()
        self.w("## 1. 컨셉")
        self.w()
        self.w(f"- 한 줄 요약: {song.get('summary') or d.get('comment', '')}")
        self.w("- 감정 곡선: " + " / ".join(f"{s['name']} {s['energy']['level']}" for s in self.sections() if s.get("energy")))
        motifs = self.ir.get("motifs", [])
        if motifs:
            self.w("- 핵심 모티프: " + "; ".join(f"{m['name']} ({', '.join(self.nm(p) for _, _, p in m.get('notes', [])[:6])})"
                                              + (f" {m['note']}" if m.get("note") else "") for m in motifs[:3]))
        self.w()
        self.w("## 2. 글로벌 설정")
        self.w()
        rows = [["BPM", f"{bpm:g}" + ("" if const else " (템포 지도: " + ", ".join(f"{self.pos(x['beat'])} {x['bpm']:g}" for x in tempo) + ")")],
                ["박자", f"{ts[0]}/{ts[1]}" + ("" if not d.get("meterChanges") else " (바뀜 있음)")],
                ["조성", d.get("key", song.get("key", "-"))], ["총 마디", f"{self.bars()} + 끝 울림 {d.get('render', {}).get('tailSeconds', 0):g}초"]]
        loop = d.get("metadata", {}).get("loop")
        if loop:
            a, z = loop["startBeat"], loop["endBeat"]
            sec = lambda beat: beat * 60.0 / bpm
            rows.append(["루프 구간", f"{self.bar_of(a)}~{self.bar_of(z - 1e-6)}마디 (`metadata.loop`: `startBeat` {a:g}, `endBeat` {z:g})"])
            if const:
                n_bars = (z - a) / self.bb
                rows.append(["루프 길이", f"{n_bars:g}마디 × {self.bb:g}박 × 60 / {bpm:g} = **{sec(z - a):.1f}초**"])
                rows.append(["루프 샘플 (48 kHz)", f"시작 {round(sec(a) * 48000):,} / 길이 {round(sec(z - a) * 48000):,}"])
                rows.append(["루프 샘플 (44.1 kHz)", f"시작 {round(sec(a) * 44100):,} / 길이 {round(sec(z - a) * 44100):,}"])
        perf = d.get("performance", {})
        rows.append(["휴머나이즈", ", ".join(f"`{k}` {v}" for k, v in perf.items() if "umanize" in k and k != "humanizeSeed") or "엔진 기본 (꺼짐)"])
        rows.append(["위치 표기", f"`마디.박.틱`, 1박 = {self.ppq}틱. 곡 JSON 의 박 = (마디-1)×{self.bb:g} + (박-1) + 틱/{self.ppq}"])
        master = d.get("master", {})
        rows.append(["마스터", (f"`targetLufs` {master['targetLufs']:g}, " if "targetLufs" in master else "") + ", ".join(e["type"] for e in master.get("effects", []))])
        rows.append(["버스", ", ".join(f"{bus['name']} ({', '.join(e['type'] for e in bus.get('effects', []))})" for bus in d.get("buses", []))])
        self.table(["항목", "값"], rows)
        self.w("## 3. 구조표")
        self.w()
        rows = []
        for s in self.sections():
            a, z = s["beats"]
            active = [t["name"] for t in self.tracks() if any(a - 1e-6 <= x["startBeat"] < z - 1e-6 for x in t["notes"])]
            e = s.get("energy", {})
            rows.append([s["name"], f"{s['bars'][0]}~{s['bars'][1]}", f"{e.get('level', '-')} ({e.get('trend', '-')})", ", ".join(active), s.get("purpose", "")])
        self.table(["섹션", "마디", "에너지", "활성 트랙", "역할"], rows)
        self.w("## 4. 트랙 편성")
        self.w()
        rows = []
        for k, t in enumerate(self.tracks(), 1):
            ps = [x["pitch"] for x in t["notes"]]
            fx = []
            for e in t.get("effects", []):
                if e["type"] == "eq":
                    bands = ", ".join(f"{bd.get('freqHz', 0):g} Hz {bd.get('gainDb', 0):+g}" for bd in e.get("bands", []))
                    fx.append(f"EQ ({e.get('lowCutHz', 0):g} Hz 아래 차단" + (f", {bands}" if bands else "") + ")")
                else:
                    keys = [k2 for k2 in e if k2 not in ("type", "automation", "comment")]
                    fx.append(f"{e['type']} (" + ", ".join(f"{k2} {e[k2]}" for k2 in keys[:4]) + ")")
            for s in t.get("sends", []):
                fx.append(f"{s['bus']} 센드 {s['levelDb']:g} dB")
            mix = t.get("mixer", {})
            syn = t.get("synth")
            inst = f"`{t['instrument']}`" + (f" (트랙 아티큘레이션 {t['articulation']})" if t.get("articulation") else "")
            if syn:
                inst += " 덮는 값: " + ", ".join(f"{k2} {v}" for k2, v in syn.items())
            rng = f"{self.nm(min(ps))}~{self.nm(max(ps))}" if not self.is_drum(t) else "북 건반 " + ", ".join(str(p) for p in sorted(set(ps)))
            rows.append([k, t["name"], ", ".join(sorted(self.roles(t))) or "-", inst, rng, f"{mix.get('gainDb', 0):g}" + (" (곡선)" if mix.get("automation") else ""),
                         f"{mix.get('pan', 0):g}", "; ".join(fx) or "없음"])
        self.table(["#", "트랙", "역할", "악기", "음역 (MIDI)", "gainDb", "팬 (-1~1)", "효과와 센드"], rows)
        self.w("## 5. 코드 진행")
        self.w()
        rows = []
        tonic, minor = key_of(self.data.get("key", "C major"))
        for seg in self.ir.get("harmony", []):
            a, length = seg["beat"], seg["beats"]
            voicing = self.voicing_at(a + 0.01)
            bass = self.bass_at(a + 0.01)
            first, last = self.bar_of(a), self.bar_of(a + length - 1e-6)
            beat_in = a - (first - 1) * self.bb
            span = f"{first}" if first == last else f"{first}~{last}"
            rows.append([span, f"{beat_in + 1:g}", seg["chord"], roman(seg["chord"], tonic, minor), " ".join(self.nm(p) for p in voicing) or "-",
                         self.nm(bass) if bass is not None else "-"])
        self.table(["마디", "박", "코드", "로마숫자", "보이싱 (낮은 음→높은 음, 화음 트랙)", "베이스음"], rows)
        self.w("▶ 다음: Part 2")
        self.w()

    def sounding(self, beat, pred):
        out = []
        for t in self.tracks():
            if self.is_drum(t) or not pred(t):
                continue
            for x in t["notes"]:
                if x["startBeat"] <= beat < x["startBeat"] + x["durationBeat"]:
                    out.append(x["pitch"])
        return out

    def voicing_at(self, beat):
        ps = self.sounding(beat, lambda t: self.roles(t) & {"pad", "harmony"})
        if not ps:
            ps = self.sounding(beat, lambda t: not (self.roles(t) & {"main-melody", "bass", "atmosphere"}))
        return sorted(set(ps))

    def bass_at(self, beat):
        ps = self.sounding(beat, lambda t: "bass" in self.roles(t))
        return min(ps) if ps else None

    # ---- Part 2, 3 ----
    def part2(self):
        self.w("# Part 2. 선율, 동기, 베이스 (모든 음)")
        self.w()
        self.w("노트 줄은 `위치 | 음 | 길이(틱) | 벨로시티 | 아티큘레이션 | 붙임줄`.")
        self.w()
        k = 0
        for t in self.tracks():
            if self.is_drum(t) or not (self.roles(t) & FULL_ROLES):
                continue
            k += 1
            self.w(f"## 6-{k}. {t['name']} ({', '.join(sorted(self.roles(t)))}, `{t['instrument']}`)")
            self.w()
            for sid, (s, sel) in self.by_section(t["notes"]).items():
                self.w(f"### {s['name']} ({s['bars'][0]}~{s['bars'][1]}마디)")
                self.note_rows(sel)
                self.w()
        self.w("▶ 다음: Part 3")
        self.w()
        return k

    def part3(self, k):
        self.w("# Part 3. 반주, 화음, 소리, 북")
        self.w()
        for t in self.tracks():
            if self.is_drum(t) or self.roles(t) & FULL_ROLES:
                continue
            k += 1
            self.w(f"## 6-{k}. {t['name']} ({', '.join(sorted(self.roles(t))) or '-'}, `{t['instrument']}`)")
            self.w()
            self.write_patterns(t)
            self.w()
        self.w("## 7. 드럼")
        self.w()
        for t in self.tracks():
            if not self.is_drum(t):
                continue
            self.w(f"### {t['name']} (`{t['instrument']}`)")
            self.w()
            self.drum_grid(t)
            self.w()
        self.w("▶ 다음: Part 4")
        self.w()

    # ---- Part 4 ----
    def part4(self):
        self.w("# Part 4. 곡선과 검증")
        self.w()
        self.w("## 8. 오토메이션과 다이내믹")
        self.w()
        self.w("곡선의 점: `위치 | 값 | 모양`. 모양은 그 점에서 다음 점까지 (`linear` 곧게, `smooth` 매끄럽게, `exponential`, `step` 다음 점까지 붙든다). "
               "CC1 은 셈여림, CC11 은 볼륨 ((값/127)² 의 크기), CC64 는 페달, CC74 는 필터 (127 이 프리셋, 0 이 여섯 옥타브 아래), 피치 벤드는 -1~1 "
               "(트랙의 bendRange 반음). 점이 많은 곡선은 `위치=값` 을 여섯씩 한 줄에 적었다.")
        self.w()
        rows = []
        for t in self.tracks():
            for cc in LANES:
                if t.get(cc):
                    pts = t[cc]
                    rows.append([t["name"], cc, f"{self.pos(pts[0]['beat'])} ~ {self.pos(pts[-1]['beat'])}", f"{pts[0]['value']:g} → {pts[-1]['value']:g}", len(pts)])
            auto = t.get("mixer", {}).get("automation", {})
            for key, pts in auto.items():
                rows.append([t["name"], f"페이더 {key}", f"{self.pos(pts[0]['beat'])} ~ {self.pos(pts[-1]['beat'])}", f"{pts[0]['value']:g} → {pts[-1]['value']:g}", len(pts)])
        for e in self.data.get("master", {}).get("effects", []):
            for key, pts in (e.get("automation") or {}).items():
                rows.append(["마스터", f"{e['type']} {key}", f"{self.pos(pts[0]['beat'])} ~ {self.pos(pts[-1]['beat'])}", f"{pts[0]['value']:g} → {pts[-1]['value']:g}", len(pts)])
        self.table(["트랙", "파라미터", "구간", "시작값→끝값", "점"], rows)
        for t in self.tracks():
            for cc in LANES:
                if t.get(cc):
                    self.lane_points(f"{t['name']} {cc}", t[cc])
            for key, pts in t.get("mixer", {}).get("automation", {}).items():
                self.lane_points(f"{t['name']} 페이더 {key}", pts)
            for k, e in enumerate(t.get("effects", [])):
                for key, pts in (e.get("automation") or {}).items():
                    self.lane_points(f"{t['name']} 효과 {k}:{e['type']} {key}", pts)
        for e in self.data.get("master", {}).get("effects", []):
            for key, pts in (e.get("automation") or {}).items():
                self.lane_points(f"마스터 {e['type']} {key}", pts)
        self.w()
        self.checks()

    def checks(self):
        self.w("## 9. 자체 검증")
        self.w()
        out = []
        crossing = [(t["name"], x) for t in self.tracks() if not self.is_drum(t) for x in t["notes"]
                    if x["startBeat"] + x["durationBeat"] - self.bar_of(x["startBeat"]) * self.bb > LEGATO + 1e-6]
        out.append((True, "마디 경계를 넘는 노트가 없다 (타이는 명시)",
                    f"마디줄을 넘는 음 {len(crossing)}개는 노트 줄에 붙임줄로 적었다. 북의 울림과 레가토 겹침 ({int(LEGATO * self.ppq)}틱 안) 은 세지 않는다"))
        melody = [t for t in self.tracks() if "main-melody" in self.roles(t)]
        bad, counted = [], 0
        strong = {0.0} | ({2.0} if self.bb == 4 else set())
        for t in melody:
            for x in t["notes"]:
                off = x["startBeat"] - (self.bar_of(x["startBeat"]) - 1) * self.bb
                if not any(abs(off - s) < 1e-6 for s in strong):
                    continue
                seg = self.chord_at(x["startBeat"])
                if not seg:
                    continue
                counted += 1
                root = chord_root(seg["chord"])
                iv = (x["pitch"] - root) % 12 if root is not None else None
                if x["pitch"] % 12 not in seg["tones"] and iv not in (2, 5, 9):
                    bad.append(f"{t['name']} {self.pos(x['startBeat'])} {self.nm(x['pitch'])} ({seg['chord']})")
        out.append((not bad, "강박 멜로디 음이 모두 코드톤이거나 허용된 텐션이다",
                    f"선율 역할 트랙의 강박 음 {counted}개를 셌다" + (": " + ", ".join(bad[:12]) + (" 등" if len(bad) > 12 else "") if bad else "")))
        bass_tracks = [t for t in self.tracks() if "bass" in self.roles(t) and not self.is_drum(t)]
        par = self.parallels(melody, bass_tracks)
        out.append((not par, "연속 5도와 8도가 없다 ([의도] 제외)",
                    "선율과 가장 낮은 베이스 가운데 하나라도 바뀌는 자리마다 이웃한 음정을 셌다" + (": " + ", ".join(par[:12]) if par else ", 없음")))
        rng = []
        for t in self.tracks():
            lim = self.instruments.get(t["instrument"], {}).get("range")
            if lim:
                for x in t["notes"]:
                    if not lim[0] <= x["pitch"] <= lim[1]:
                        rng.append(f"{t['name']} {self.pos(x['startBeat'])} {self.nm(x['pitch'])}")
        out.append((not rng, "모든 트랙이 지정 음역을 지킨다",
                    "SFZ 악기는 녹음이 있는 건반 안, 내장 신스는 음역 제한이 없다" + (": " + ", ".join(rng[:10]) if rng else "")))
        loop = self.data.get("metadata", {}).get("loop")
        if loop:
            same = self.seam_bars(loop["startBeat"], loop["endBeat"])
            seam = ""
            rep = os.path.join(REPO, "build", "songs", os.path.splitext(os.path.basename(self.ctx.json_file))[0] + "_loop.report.json")
            if os.path.exists(rep):
                r = json.load(open(rep))
                seam = f" 렌더링한 반복 파일의 이음매 차이 {r['loop']['seamDb']:g} dB (그 자리의 크기 {r['loop']['seamLevelDb']:g} dB)."
            out.append((same > 0, "루프 끝 마디와 시작 마디의 노트를 나란히 놓고 연결을 확인했다",
                        f"반복 끝 앞의 {same}마디가 반복 시작 앞의 {same}마디와 음과 곡선까지 같다." + seam))
            bpm = self.data["tempo"][0]["bpm"]
            out.append((True, "루프 길이", f"{(loop['endBeat'] - loop['startBeat']) * 60 / bpm:.1f}초"))
        for ok, title, note in out:
            self.w(f"- [{'x' if ok else ' '}] {title}: {note}")
        self.w()
        return out

    def parallels(self, melody, bass_tracks):
        if not melody or not bass_tracks:
            return []
        mel = sorted((x["startBeat"], x["startBeat"] + x["durationBeat"], x["pitch"]) for t in melody for x in t["notes"])
        bass = sorted((x["startBeat"], x["startBeat"] + x["durationBeat"], x["pitch"]) for t in bass_tracks for x in t["notes"])

        def at(line, t, low):
            c = [(s, p) for s, e, p in line if s <= t + 1e-6 < e]
            if not c:
                return None
            return min(p for _, p in c) if low else max(c)[1]

        events = sorted({round(s, 5) for s, _, _ in mel} | {round(s, 5) for s, _, _ in bass})
        pairs = []
        for t in events:
            m, b = at(mel, t, False), at(bass, t, True)
            if m is None or b is None:
                pairs.append(None)
            elif not pairs or pairs[-1] is None or pairs[-1][1:] != (m, b):
                pairs.append((t, m, b))
        out = []
        for a, z in zip(pairs, pairs[1:]):
            if a is None or z is None:
                continue
            (t1, m1, b1), (t2, m2, b2) = a, z
            if m1 == m2 or b1 == b2:
                continue
            i1, i2 = (m1 - b1) % 12, (m2 - b2) % 12
            if i1 == i2 and i1 in (0, 7) and (m2 - m1) * (b2 - b1) > 0:
                out.append(f"{self.pos(t1)}→{self.pos(t2)}")
        return out

    def seam_bars(self, start, end):
        """반복 끝 앞과 반복 시작 앞이 같은 마디 수 (음과 곡선, 끝의 같은 값 점은 뺀다)"""
        def content(a, z):
            items = []
            for t in self.tracks():
                items += [(t["name"], round(x["startBeat"] - a, 4), x["pitch"], x["velocity"], round(x["durationBeat"], 4)) for x in t["notes"]
                          if a - 1e-6 <= x["startBeat"] < z - 1e-6]
                for cc in LANES:
                    pts = t.get(cc) or []
                    if len(pts) > 1 and pts[-1]["value"] == pts[-2]["value"]:
                        pts = pts[:-1]
                    items += [(t["name"], cc, round(p["beat"] - a, 3), round(p["value"], 3)) for p in pts if a - 1e-6 <= p["beat"] < z - 1e-6]
            return sorted(map(str, items))
        n = 0
        for k in range(1, 17):
            span = k * self.bb
            if start - span < -1e-6:
                break
            if content(start - span, start) != content(end - span, end):
                break
            n = k
        return n

    def write(self):
        d = self.data
        self.w(f"# 「{d.get('title', '')}」 배치 명세")
        self.w()
        self.w(f"> `tools/placement-spec.py` 가 곡 JSON (`{os.path.relpath(self.ctx.json_file, REPO)}`) 과 Music IR 에서 쓴다. 손으로 고치지 않는다. "
               "같은 곡을 앱에서 바로 열 수도 있다.")
        self.w(f"> 위치는 `마디.박.틱` (마디와 박은 1 부터, 틱은 0 부터, 1박 = {self.ppq}틱), 길이는 틱, 음은 이름(MIDI), C4 = 60.")
        self.w()
        self.part1()
        k = self.part2()
        self.part3(k)
        self.part4()
        return "\n".join(self.lines) + "\n"


def spans(bars):
    out, run = [], [bars[0], bars[0]]
    for x in bars[1:]:
        if x == run[1] + 1:
            run[1] = x
        else:
            out.append(run)
            run = [x, x]
    out.append(run)
    return ", ".join(f"{a}" if a == z else f"{a}~{z}" for a, z in out)


def key_of(text):
    m = re.match(r"([A-G])([#b]?)\s*(major|minor)?", text or "C major")
    if not m:
        return 0, False
    pc = (PC[m.group(1)] + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0)) % 12
    return pc, m.group(3) == "minor"


def chord_root(name):
    m = re.match(r"([A-G])([#b]?)", name)
    if not m:
        return None
    return (PC[m.group(1)] + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0)) % 12


def roman(name, tonic, minor):
    root = chord_root(name)
    if root is None:
        return "-"
    iv = (root - tonic) % 12
    scale = [0, 2, 3, 5, 7, 8, 10] if minor else [0, 2, 4, 5, 7, 9, 11]
    nums = ["I", "II", "III", "IV", "V", "VI", "VII"]
    if iv in scale:
        num = nums[scale.index(iv)]
    else:
        lower = [d for d in range(7) if scale[d] == (iv + 1) % 12]
        num = ("b" + nums[lower[0]]) if lower else ("#" + nums[[d for d in range(7) if scale[d] == (iv - 1) % 12][0]])
    body = re.sub(r"^[A-G][#b]?", "", name.split("/")[0])
    if body.startswith("m") and not body.startswith("maj"):
        num = num.lower()
        body = body[1:]
    return num + body + ("/" + name.split("/")[1] if "/" in name else "")


def instrument_info():
    """music-render instruments 의 건반 이름과 SFZ 의 건반 범위"""
    exe = os.path.join(REPO, "build", "engine", "music-render")
    info = {}
    try:
        items = json.loads(subprocess.run([exe, "instruments"], capture_output=True, text=True, timeout=60).stdout)
        items = items if isinstance(items, list) else items.get("instruments", [])
    except Exception:
        items = []
    for x in items:
        entry = {"keyNames": x.get("keyNames")}
        f = x.get("file")
        if x.get("type") == "sfz" and f and os.path.exists(f):
            entry["range"] = sfz_range(f)
        info[x["name"]] = entry
    return info


def sfz_range(path):
    names = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}

    def num(s):
        s = s.strip().lower()
        if s.lstrip("-").isdigit():
            return int(s)
        m = re.match(r"([a-g])(#|b)?(-?\d+)", s)
        if not m:
            return None
        return names[m.group(1)] + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0) + 12 * (int(m.group(3)) + 1)

    def text(p, depth=0):
        try:
            t = open(p, errors="ignore").read()
        except OSError:
            return ""
        if depth > 4:
            return t
        d = os.path.dirname(p)
        return re.sub(r'#include "([^"]+)"', lambda m: text(os.path.join(d, m.group(1)), depth + 1), t)

    vals = [num(v) for v in re.findall(r"\b(?:lokey|hikey|key)=(\S+)", text(path))]
    vals = [v for v in vals if v is not None]
    return (min(vals), max(vals)) if vals else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("song")
    ap.add_argument("--out")
    ap.add_argument("--ppq", type=int, default=480)
    args = ap.parse_args()
    ctx = cli.resolve(args.song)
    folder = os.path.basename(os.path.dirname(ctx.json_file))
    out = args.out or os.path.join(REPO, "docs", "songs", f"{folder}-spec.md")
    spec = Spec(ctx, args.ppq)
    text = spec.write()
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"{out}: {len(spec.lines)} lines")


if __name__ == "__main__":
    main()
