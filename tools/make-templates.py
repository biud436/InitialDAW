#!/usr/bin/env python3
"""곡 템플릿을 만든다: 지금까지의 곡에서 편성, 믹서, 효과, 버스, 마스터, 템포와 박자만 남긴 빈 곡 (templates/<id>.json).

곡에서 가져오는 것: 트랙(이름, 악기, 아티큘레이션, 옮김, 믹서, 센드, 효과, 신스 값, 플러그인 설정, 연주 처리), 버스, 마스터,
첫 템포, 박자, 연주 처리, 꼬리 길이. 지우는 것: 음표, 템포 변화, 곡의 끝 박, 주석, 곡의 반복 구간.
컨트롤러 곡선은 레인마다 곡 안의 중앙값 한 점으로 남긴다 (트랙 게인이 그 크기에 맞춰져 있다). 페달과 피치 벤드는 지운다.
되풀이 템플릿은 4마디 도입 뒤 32마디 반복 구간을 둔다.

사용법:
  tools/make-templates.py            # templates/ 를 다시 쓴다
  tools/make-templates.py --check    # 디스크의 템플릿이 생성 결과와 같은지만 본다 (다르면 종료 코드 1)
"""
import argparse
import copy
import json
import os
import statistics
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "templates")

# 순서, id, 바탕 곡, 이름, 설명, 되풀이, 고칠 것 (뺄 트랙, 템포)
TEMPLATES = [
    ("empty", "snow-kingdom/snow_kingdom.json", "빈 곡",
     "그랜드 피아노 트랙 하나와 방, 홀 잔향만 있는 빈 곡입니다.",
     False, {"keep": ["Piano"], "bpm": 120}),
    ("piano-solo", "snow-kingdom/snow_kingdom.json", "피아노 독주",
     "그랜드 피아노 두 트랙에 선율(Piano Melody)과 반주(Piano)를 나눠 둡니다. 선율만 따로 고치고 크기를 나눌 수 있습니다.",
     False, {}),
    ("orchestral-battle", "blade-rush/blade_rush.json", "관현악 전투",
     "Sonatina 현악 다섯 성부, 호른 합주, 팀파니, 관현악 큰북과 타악기. 무대와 홀 잔향.",
     True, {}),
    ("boss-battle", "shattered-spire/shattered_spire.json", "보스 전투",
     "어두운 관현악(현악, 호른, 낮은 그랜드 피아노, 큰 합창, 팀파니, 예고편 북)에 톱니파 베이스와 서브를 더했습니다.",
     True, {}),
    ("dungeon", "forgotten-glyphs/forgotten_glyphs.json", "던전",
     "낮은 현의 지속음, 그랜드 피아노, 먼 합창, 금속 타악기(징, 브레이크 드럼, 벨 트리, 활로 긋는 심벌), 바이올린 하모닉스, 바람. 효과음 아래에 깔리도록 작게 맞춥니다.",
     True, {}),
    ("town-dance", "salt-wind/salt_wind.json", "마을 춤곡",
     "나일론 기타, 독주 플루트와 클라리넷, 하프, 현악, 큰북과 탬버린. 6/8 의 템포 108 은 점4분음표 72 입니다.",
     True, {}),
    ("scherzo", "quill-mischief/quill_mischief.json", "스케르초",
     "클라리넷과 플루트, 첼레스타, 하프, 현악(피치카토와 활), 하모닉스, 팀파니, 심벌.",
     True, {}),
    ("orchestral-theme", "far-horizon/far_horizon.json", "관현악 주제곡",
     "목관(플루트, 오보에, 클라리넷), 독주 호른과 호른 합주, Sonatina 현악, 하프, 첼레스타, 하모닉스, 팀파니와 타악기.",
     False, {}),
    ("song-form", "rusted-wings/rusted_wings.json", "노래 형식",
     "독주 클라리넷이 노래하고 그랜드 피아노와 나일론 기타가 받칩니다. 현악, 합창, 글로켄슈필, 타악기.",
     False, {}),
    ("electronic", "blue-signal/blue_signal.json", "전자 음악",
     "전자 북, 서브와 톱니파 베이스, 슈퍼소 화음과 리드, 패드, 플럭, 피아노. 킥이 잔향과 베이스를 사이드체인으로 누릅니다.",
     False, {}),
    ("liquid-dnb", "neon-rain/neon_rain.json", "리퀴드 드럼앤베이스",
     "브레이크비트용 전자 북, 서브와 중음 베이스, 패드, 현악, 비브라폰 둘, 하프, 빗방울 플럭과 빗소리.",
     False, {"drop": ["Solo Violin"]}),
    ("dubstep", "ashen-rift/ashen_rift.json", "덥스텝",
     "하프타임 전자 북과 예고편 타격, 서브, CC74 로 여닫는 워블 베이스, 피치 벤드 그로울, 패드, 그랜드 피아노 두 트랙, 합창.",
     False, {}),
    ("minimal-electronic", "glass-circuit/glass_circuit.json", "미니멀 전자 음악",
     "작은 북(킥, 스냅, 틱, 셰이커), 서브와 맥박 베이스, 패드, 현 신스, 폴리 키 시퀀스, 유리 벨, 그랜드 피아노 두 트랙.",
     False, {}),
]

KEEP_LANES = {"cc1", "cc11", "cc21", "cc74"}
DROP_LANES = {"cc64", "pitchBend"}


def strip_comments(value):
    if isinstance(value, dict):
        return {k: strip_comments(v) for k, v in value.items() if k != "comment"}
    if isinstance(value, list):
        return [strip_comments(v) for v in value]
    return value


def lane_level(points):
    values = [p["value"] for p in points if isinstance(p, dict) and isinstance(p.get("value"), (int, float))]
    if not values:
        return None
    v = statistics.median(values)
    return round(v) if abs(v - round(v)) < 1e-9 else round(v, 2)


def template_track(track):
    out = {}
    for key, value in track.items():
        if key in ("notes", "comment"):
            continue
        if key in DROP_LANES or (key.startswith("cc") and key[2:].isdigit() and key not in KEEP_LANES):
            continue
        if key in KEEP_LANES:
            level = lane_level(value)
            if level is not None:
                out[key] = [{"beat": 0, "value": level}]
            continue
        if key == "automation":   # 곡선이 "automation" 안에 있는 곡
            lanes = {}
            for lane, points in value.items():
                if lane in KEEP_LANES and (level := lane_level(points)) is not None:
                    lanes[lane] = [{"beat": 0, "value": level}]
            if lanes:
                out["automation"] = lanes
            continue
        out[key] = strip_comments(copy.deepcopy(value))
    out["notes"] = []
    return out


def build(order, tid, source, name, description, loop, fix):
    song = json.load(open(os.path.join(ROOT, "songs", source)))
    tracks = song["tracks"]
    if "keep" in fix:
        tracks = [t for t in tracks if t["name"] in fix["keep"]]
    if "drop" in fix:
        tracks = [t for t in tracks if t["name"] not in fix["drop"]]
    used_buses = {s["bus"] for t in tracks for s in t.get("sends", [])}
    tempo = song["tempo"]
    bpm = fix.get("bpm", tempo[0]["bpm"] if isinstance(tempo, list) else tempo)
    beats_per_bar, unit = song.get("timeSignature", [4, 4])
    bar = beats_per_bar * 4 / unit
    render = {k: v for k, v in song.get("render", {}).items() if k in ("tailSeconds", "fadeOutSeconds")}
    data = {
        "title": "",
        "metadata": {"template": {"id": tid, "name": name, "description": description, "order": order}},
        "tempo": bpm,
        "timeSignature": [beats_per_bar, unit],
    }
    if loop:
        data["metadata"]["loop"] = {"startBeat": 4 * bar, "endBeat": 36 * bar}
    if "performance" in song:
        data["performance"] = strip_comments(song["performance"])
    if render:
        data["render"] = render
    data["tracks"] = [template_track(t) for t in tracks]
    data["buses"] = [strip_comments(b) for b in song.get("buses", []) if b["name"] in used_buses]
    data["master"] = strip_comments(song.get("master", {}))
    return data


def main():
    ap = argparse.ArgumentParser(description="곡에서 템플릿(templates/<id>.json)을 만든다")
    ap.add_argument("--check", action="store_true", help="디스크의 템플릿이 생성 결과와 같은지만 본다")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    stale = []
    for order, (tid, source, name, description, loop, fix) in enumerate(TEMPLATES, start=1):
        text = json.dumps(build(order, tid, source, name, description, loop, fix), ensure_ascii=False, indent=1) + "\n"
        path = os.path.join(OUT, tid + ".json")
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if old != text:
            stale.append(tid)
            if not args.check:
                open(path, "w", encoding="utf-8").write(text)
    known = {t[0] + ".json" for t in TEMPLATES}
    extra = sorted(f for f in os.listdir(OUT) if f.endswith(".json") and f not in known)
    print(json.dumps({"ok": not (args.check and (stale or extra)), "templates": len(TEMPLATES),
                      ("stale" if args.check else "written"): stale, "unknown": extra}, ensure_ascii=False))
    return 1 if args.check and (stale or extra) else 0


if __name__ == "__main__":
    sys.exit(main())
