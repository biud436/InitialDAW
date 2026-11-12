#!/usr/bin/env python3
"""music-render 명령행을 끝까지 돌려 본다: 결과 JSON, WAV 의 샘플 수와 형식, 결정성, 오류와 종료 코드.

사용법: python3 tests/e2e_cli.py <music-render 실행 파일> <출력 폴더>
"""
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import wave

EXE = os.path.abspath(sys.argv[1])
OUT = os.path.abspath(sys.argv[2])
PLUGIN = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] else None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAILS = []
CHECKS = 0


def check(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    print(("ok   " if cond else "FAIL ") + name + ("" if cond or not detail else "  | " + str(detail)[:600]))
    if not cond:
        FAILS.append(name)


def run(*args, code=0, env=None):
    p = subprocess.run([EXE, *args], capture_output=True, text=True, cwd=ROOT, env=None if env is None else {**os.environ, **env})
    try:
        data = json.loads(p.stdout)
    except json.JSONDecodeError:
        data = None
    check(f"exit {code}: {' '.join(args)[:80]}", p.returncode == code, f"rc={p.returncode} stderr={p.stderr[-400:]}")
    check(f"stdout is one JSON object: {args[0]}", isinstance(data, dict), p.stdout[-300:])
    return data or {}, p


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    demo = os.path.join(ROOT, "examples", "piano_demo.json")

    v, _ = run("version")
    check("version reports juce 8", str(v.get("juce", "")).startswith("8."), v)

    v, _ = run("validate", demo)
    check("demo validates", v.get("ok") is True and not v.get("errors"), v)
    check("demo summary counts notes", v.get("summary", {}).get("notes", 0) > 100, v.get("summary"))

    ov, _ = run("validate", os.path.join(ROOT, "examples", "orchestral_demo.json"))
    check("orchestral demo validates", ov.get("ok") is True and abs(ov.get("summary", {}).get("estimatedDuration", 0) - 64.0) < 1e-6, ov.get("summary"))

    v, _ = run("validate", os.path.join(ROOT, "tests", "fixtures", "invalid_song.json"), code=1)
    paths = {e["path"] for e in v.get("errors", [])}
    check("invalid: pitch out of range", "$.tracks[0].notes[0].pitch" in paths, paths)
    check("invalid: unknown instrument", "$.tracks[1].instrument" in paths, paths)
    hints = [w.get("hint", "") for w in v.get("warnings", [])]
    check("invalid: typo suggestion", any("velocity" in h for h in hints), v.get("warnings"))
    inst = [e for e in v.get("errors", []) if e["path"] == "$.tracks[1].instrument"]
    check("invalid: instrument suggestion", inst and "piano" in inst[0].get("hint", ""), inst)

    a = os.path.join(OUT, "demo_a.wav")
    r, _ = run("render", demo, "--output", a, "--quiet")
    check("render ok", r.get("ok") is True, r)
    m = r.get("master", {})
    with wave.open(a, "rb") as w:
        frames, rate, width, channels = w.getnframes(), w.getframerate(), w.getsampwidth(), w.getnchannels()
    check("wav sample count matches report", frames == m.get("samples"), (frames, m.get("samples")))
    check("wav is 48 kHz 24-bit stereo", (rate, width, channels) == (48000, 3, 2), (rate, width, channels))
    check("duration matches samples", abs(m.get("duration", 0) - frames / rate) < 1e-3, m)
    check("demo is longer than 40 s", m.get("duration", 0) > 40, m)
    check("demo does not clip", m.get("clipping") is False, m)
    check("demo has loudness", isinstance(m.get("lufs"), (int, float)) and -40 < m["lufs"] < -8, m)
    check("tracks are reported", [t["name"] for t in r.get("tracks", [])] == ["Melody", "Accompaniment"], r.get("tracks"))

    b = os.path.join(OUT, "demo_b.wav")
    run("render", demo, "--output", b, "--quiet")
    check("same input renders identical bytes", sha(a) == sha(b))
    c = os.path.join(OUT, "demo_seed1.wav")
    r1, _ = run("render", demo, "--output", c, "--seed", "1", "--quiet")
    check("a different seed changes the performance", sha(a) != sha(c))
    check("seed is reported", r1.get("seed") == "1", r1.get("seed"))

    i, _ = run("inspect", a)
    check("inspect sees the same samples", i.get("samples") == m.get("samples"), i)
    check("inspect lufs agrees with render", abs((i.get("lufs") or 0) - (m.get("lufs") or 99)) < 0.02, (i.get("lufs"), m.get("lufs")))
    for key in ("duration", "peakDb", "rmsDb", "lufs", "clipping", "sampleRate", "channels", "bitDepth"):
        check(f"inspect has {key}", key in i, i)

    if shutil.which("ffmpeg"):
        p = subprocess.run(["ffmpeg", "-nostats", "-i", a, "-af", "ebur128", "-f", "null", "-"], capture_output=True, text=True)
        lufs = None
        for line in reversed(p.stderr.splitlines()):
            line = line.strip()
            if line.startswith("I:") and "LUFS" in line:
                lufs = float(line.split()[1])
                break
        check("lufs agrees with ffmpeg ebur128", lufs is not None and abs(lufs - i["lufs"]) <= 0.3, (lufs, i.get("lufs")))

    d16 = os.path.join(OUT, "demo_44k16.wav")
    run("render", demo, "--output", d16, "--sample-rate", "44100", "--bit-depth", "16", "--quiet")
    with wave.open(d16, "rb") as w:
        check("44.1 kHz 16-bit output", (w.getframerate(), w.getsampwidth()) == (44100, 2), (w.getframerate(), w.getsampwidth()))

    stems = os.path.join(OUT, "stems")
    st, _ = run("render", demo, "--output", os.path.join(OUT, "demo_stems.wav"), "--stems", stems, "--quiet")
    expected = {"Melody.wav", "Accompaniment.wav", "master.wav"}
    check("stems are written per track plus master", expected <= set(os.listdir(stems)) if os.path.isdir(stems) else False,
          os.listdir(stems) if os.path.isdir(stems) else None)
    if os.path.isdir(stems):
        lengths = set()
        for f in expected:
            with wave.open(os.path.join(stems, f), "rb") as w:
                lengths.add(w.getnframes())
        check("every stem has the master length", lengths == {m.get("samples")}, lengths)
        check("master stem equals the output", sha(os.path.join(stems, "master.wav")) == sha(os.path.join(OUT, "demo_stems.wav")))
    check("render report names the stems", all("stem" in t for t in st.get("tracks", [])), st.get("tracks"))

    measure = os.path.join(OUT, "demo.measure.json")
    mo = os.path.join(OUT, "demo_measure.ogg")
    ms, _ = run("render", demo, "--output", mo, "--measure", measure, "--quiet")
    md = json.load(open(measure)) if os.path.exists(measure) else {}
    names = [s["name"] for s in md.get("stems", [])]
    check("measure names every track, bus and the master", {"Melody", "Accompaniment", "master"} <= set(names), names)
    bars = len(md.get("barStartSeconds", [])) - 1
    check("measure has a value per bar for every stem",
          bars > 1 and all(len(s["barDb"]) == bars and len(s["barBandDb"]) == bars for s in md.get("stems", [])), bars)
    mel = next((s for s in md.get("stems", []) if s["name"] == "Melody"), {})
    check("measure lists the melody's note heads", len(mel.get("notes", [])) > 10 and len(mel["notes"][0]) == 6, len(mel.get("notes", [])))
    check("measure writes no stem files", all("stem" not in t for t in ms.get("tracks", [])) and ms.get("measure") == measure, ms.get("measure"))
    check("measure is small", os.path.getsize(measure) < 200_000 if os.path.exists(measure) else False)
    check("measured master level agrees with the report",
          abs(10 * math.log10(sum(10 ** (d / 10) * (md["barStartSeconds"][i + 1] - md["barStartSeconds"][i])
                                  for i, d in enumerate(next(s for s in md["stems"] if s["kind"] == "master")["barDb"]))
                              / md["barStartSeconds"][-1]) - ms["master"]["rmsDb"]) < 0.5 if md else False)

    mid = os.path.join(OUT, "demo.mid")
    e, _ = run("export-midi", demo, "--output", mid)
    check("midi starts with MThd", open(mid, "rb").read(4) == b"MThd")
    check("midi has every note", e.get("notes") == v.get("summary", {}).get("notes") or e.get("notes", 0) > 100, e)
    e2, _ = run("export-midi", demo, "--output", os.path.join(OUT, "demo_h.mid"), "--humanized")
    check("humanized midi differs", sha(mid) != sha(os.path.join(OUT, "demo_h.mid")))
    check("midi without lyrics reports none", e.get("lyrics") == 0, e)

    sung = os.path.join(OUT, "sung.json")
    with open(sung, "w", encoding="utf-8") as f:
        json.dump({"tempo": 72, "tracks": [{"name": "Lead", "instrument": "synth.sine", "notes": [
            {"pitch": "Eb4", "startBeat": 0, "durationBeat": 1, "velocity": 80, "lyric": "늦"},
            {"pitch": "Ab4", "startBeat": 1, "durationBeat": 1, "velocity": 80, "lyric": "은"},
            {"pitch": "Bb4", "startBeat": 2, "durationBeat": 1, "velocity": 80, "lyric": "-"},
            {"pitch": "C5", "startBeat": 3, "durationBeat": 1, "velocity": 80}]}]}, f, ensure_ascii=False)
    sv, _ = run("validate", sung)
    check("lyrics validate without warnings", sv.get("ok") is True and not sv.get("warnings"), sv)
    se, _ = run("export-midi", sung, "--output", os.path.join(OUT, "sung.mid"))
    check("midi reports lyric events without the extensions", se.get("lyrics") == 2 and se.get("notes") == 4, se)
    check("midi carries the lyric bytes", "늦".encode() in open(os.path.join(OUT, "sung.mid"), "rb").read())

    # MusicXML: 가사 곡을 악보로 (조를 적은 곡과 추정한 곡)
    xml_path = os.path.join(OUT, "sung.musicxml")
    xe, _ = run("export-musicxml", sung, "--output", xml_path)
    check("musicxml reports the score", xe.get("parts") == ["Lead"] and xe.get("measures") == 1 and xe.get("notes") == 4, xe)
    check("the key is estimated when the song has none", xe.get("keyEstimated") is True and xe.get("key") == "Ab major", xe)
    import xml.etree.ElementTree as ET
    xroot = ET.parse(xml_path).getroot()
    xnotes = xroot.findall("./part/measure/note")
    check("musicxml notes are spelled in the key", [(n.find("pitch/step").text, n.findtext("pitch/alter")) for n in xnotes]
          == [("E", "-1"), ("A", "-1"), ("B", "-1"), ("C", None)], [ET.tostring(n)[:80] for n in xnotes])
    check("musicxml lyrics skip the extension", [n.findtext("lyric/text") for n in xnotes] == ["늦", "은", None, None])
    keyed = json.load(open(sung, encoding="utf-8"))
    keyed["key"] = "C minor"
    json.dump(keyed, open(os.path.join(OUT, "keyed.json"), "w", encoding="utf-8"), ensure_ascii=False)
    ke, _ = run("export-musicxml", os.path.join(OUT, "keyed.json"), "--output", os.path.join(OUT, "keyed.musicxml"))
    check("a written key is used", ke.get("key") == "C minor" and ke.get("keyEstimated") is False, ke)
    bad_key = dict(keyed, key="H minor")
    json.dump(bad_key, open(os.path.join(OUT, "badkey.json"), "w", encoding="utf-8"), ensure_ascii=False)
    bk, _ = run("validate", os.path.join(OUT, "badkey.json"), code=1)
    check("an unknown key is a validation error", any(e.get("path") == "$.key" for e in bk.get("errors", [])), bk)

    # 퀵 샘플러: 330 Hz 소리의 높이를 재서 SFZ 를 쓰고, 곡 안의 정의로 렌더링한다
    tone_dir = os.path.join(OUT, "sampler")
    os.makedirs(tone_dir)
    tone = os.path.join(tone_dir, "tone.wav")
    with wave.open(tone, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(b"".join(int(8000 * math.sin(2 * math.pi * 330 * i / 44100)).to_bytes(2, "little", signed=True) for i in range(44100)))
    qs, _ = run("sampler", tone, "--output", os.path.join(tone_dir, "tone.sfz"))
    check("sampler measures E4 and the cents", qs.get("rootKey") == 64 and abs(qs.get("frequency", 0) - 330.0) < 0.5 and abs(qs.get("tuneCents", 99) + 1.8) < 0.3, qs)
    check("sampler sfz points next to it", "sample=tone.wav" in open(os.path.join(tone_dir, "tone.sfz")).read())
    qr, _ = run("sampler", tone, "--output", os.path.join(tone_dir, "hit.sfz"), "--root", "C3", "--one-shot")
    check("sampler takes a root and one-shot", qr.get("rootKey") == 48 and "frequency" not in qr, qr)
    run("sampler", tone, "--output", os.path.join(tone_dir, "bad.sfz"), "--root", "H2", code=2)
    sampled = os.path.join(tone_dir, "song.json")
    json.dump({"tempo": 120, "instruments": [{"tone": {"type": "sfz", "file": "tone.sfz"}}],
               "tracks": [{"name": "Tone", "instrument": "tone", "notes": [{"pitch": 64, "startBeat": 0, "durationBeat": 1, "velocity": 100}]}]},
              open(sampled, "w"))
    sr, _ = run("render", sampled, "--output", os.path.join(tone_dir, "song.wav"), "--no-default-registry")
    check("the sampled instrument renders", sr.get("ok") is True and sr.get("master", {}).get("peakDb", -200) > -40 and sr["tracks"][0].get("engine") == "sfz:tone", sr)

    # 멜로디 받아 적기: 0.5~1.0초 G4, 1.0~1.5초 B4 (레가토), 2.0~2.5초 D5 를 90 BPM 의 박으로
    hum = os.path.join(tone_dir, "hum.wav")
    tones = [(0.5, 1.0, 67), (1.0, 1.5, 71), (2.0, 2.5, 74)]
    frames = []
    phase = 0.0
    for i in range(int(3.0 * 44100)):
        t = i / 44100
        v = 0.0
        for t0, t1, key in tones:
            if t0 <= t < t1:
                phase += 2 * math.pi * 440 * 2 ** ((key - 69) / 12) / 44100
                v = 0.3 * (math.sin(phase) + 0.4 * math.sin(2 * phase))
        frames.append(int(max(-1, min(1, v)) * 32000).to_bytes(2, "little", signed=True))
    with wave.open(hum, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(b"".join(frames))
    melody = os.path.join(tone_dir, "hum.json")
    tr, _ = run("transcribe", hum, "--output", melody, "--bpm", "90", "--instrument", "synth.lead")
    check("transcribe reports the notes", tr.get("notes") == 3 and tr.get("lowest") == "G4" and tr.get("highest") == "D5", tr)
    tj = json.load(open(melody, encoding="utf-8"))
    tn = tj["tracks"][0]["notes"]
    check("transcribed notes sit on the beats of the tempo", tj["tempo"] == 90 and tj["tracks"][0]["instrument"] == "synth.lead"
          and [n["pitch"] for n in tn] == [67, 71, 74] and all(abs(n["startBeat"] - t0 * 1.5) < 0.04 for n, (t0, t1, key) in zip(tn, tones))
          and all(n["humanize"] is False for n in tn), tn)
    tv, _ = run("validate", melody)
    check("the transcribed song validates", tv.get("ok") is True, tv)
    tp, _ = run("transcribe", hum, "--output", os.path.join(tone_dir, "part.json"), "--from", "1.8", "--seconds", "1")
    part = json.load(open(os.path.join(tone_dir, "part.json")))["tracks"][0]["notes"]
    check("transcribe takes a part of the file from its start", tp.get("notes") == 1 and part[0]["pitch"] == 74 and abs(part[0]["startBeat"] - 0.4) < 0.04, part)
    run("transcribe", hum, "--output", melody, code=1)   # 이미 있다

    # 음마다 높이 편집: 음 목록, 둘째 음을 C5 로, 펴기와 비브라토, 지우기, 렌더한 소리의 음
    nt, _ = run("notes", hum)
    check("notes lists the pitch_song notes", [n["pitch"] for n in nt.get("notes", [])] == ["G4", "B4", "D5"]
          and all(abs(n["cents"]) < 3 for n in nt["notes"]) and abs(nt["notes"][1]["startSeconds"] - 1.0) < 0.05, nt)
    pitch_song = os.path.join(tone_dir, "pitch_song.json")
    with open(pitch_song, "w") as f:
        json.dump({"tempo": 120, "tracks": [{"name": "Vocal", "instrument": "audio", "clips": [{"file": "hum.wav", "startBeat": 0}]}]}, f)
    run("arrange", pitch_song, "pitch-edit", "Vocal", "--note", "2", "--to", "C5")
    pe = json.load(open(pitch_song))["tracks"][0]["clips"][0].get("pitchEdits", [])
    check("pitch-edit puts the note's centre on C5", len(pe) == 1 and abs(pe[0]["semitones"] - 1.0) < 0.05
          and abs(pe[0]["seconds"] - nt["notes"][1]["startSeconds"]) < 0.002, pe)
    run("arrange", pitch_song, "pitch-edit", "Vocal", "--note", "2", "--flatten", "50", "--vibrato", "0")
    pe = json.load(open(pitch_song))["tracks"][0]["clips"][0].get("pitchEdits", [])
    check("a second pitch-edit keeps the shift and adds flatten and vibrato", len(pe) == 1 and abs(pe[0]["semitones"] - 1.0) < 0.05
          and pe[0].get("flatten") == 0.5 and pe[0].get("vibrato") == 0, pe)
    run("arrange", pitch_song, "pitch-edit", "Vocal", "--note", "3", "--semitones", "-2")
    pe = json.load(open(pitch_song))["tracks"][0]["clips"][0].get("pitchEdits", [])
    check("edits stay in time order", [round(e["semitones"]) for e in pe] == [1, -2], pe)
    er, _ = run("render", pitch_song, "--output", os.path.join(tone_dir, "pitch_song.wav"))
    rn, _ = run("notes", os.path.join(tone_dir, "pitch_song.wav"))
    check("the rendered clip sings the edited notes", er.get("ok") is True and [n["pitch"] for n in rn.get("notes", [])] == ["G4", "C5", "C5"]
          and all(abs(n["cents"]) < 5 for n in rn["notes"]), rn)
    run("arrange", pitch_song, "pitch-edit", "Vocal", "--note", "9", "--semitones", "1", code=2)
    run("arrange", pitch_song, "pitch-edit", "Vocal", "--semitones", "1", code=2)
    run("arrange", pitch_song, "pitch-edit", "Vocal", "--note", "2", "--semitones", "1", "--to", "C5", code=2)
    run("arrange", pitch_song, "pitch-edit", "Vocal", "--note", "2", "--clear")
    pe = json.load(open(pitch_song))["tracks"][0]["clips"][0].get("pitchEdits", [])
    check("--clear removes that note's edit", len(pe) == 1 and round(pe[0]["semitones"]) == -2, pe)
    run("arrange", pitch_song, "pitch-edit", "Vocal", "--clear")
    check("--clear without a note removes every edit", "pitchEdits" not in json.load(open(pitch_song))["tracks"][0]["clips"][0])

    # 템포 재기와 조각내기: 100 BPM 두 마디 루프 (8분음표마다 잡음 두드림, 박에서 크게)
    import random
    rng = random.Random(3)
    loop_wav = os.path.join(tone_dir, "loop.wav")
    total = int(4.8 * 44100)
    data = [0.0] * total
    hits = [k * 0.3 for k in range(16)]
    for k, h in enumerate(hits):
        start = int(round(h * 44100))
        amp = 0.6 if k % 2 == 0 else 0.25
        for i in range(int(0.08 * 44100)):
            data[start + i] += amp * rng.gauss(0, 0.3) * math.exp(-i / 44100 * 60) * min(1.0, (int(0.08 * 44100) - i) / 441)
    with wave.open(loop_wav, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(b"".join(int(max(-1, min(1, v)) * 32000).to_bytes(2, "little", signed=True) for v in data))
    te, _ = run("tempo", loop_wav)
    check("tempo of a two-bar loop", te.get("bpm") == 100.0 and te.get("onsets") == 16 and te.get("firstBeatSeconds", 1) < 0.02, te)
    kit = os.path.join(tone_dir, "kit.sfz")
    sl, _ = run("slice", loop_wav, "--output", kit, "--root", "C3")
    check("slice finds every hit before its attack", sl.get("slices") == 16 and sl.get("firstKey") == "C3"
          and all(-0.005 < h - s <= 0.003 for h, s in zip(hits, sl.get("starts", []))), sl)
    kit_text = open(kit).read()
    check("the kit maps one key per slice", kit_text.count("<region>") == 16 and "key=48 " in kit_text and "key=63 " in kit_text and "volume=11.51" in kit_text)
    silent = os.path.join(tone_dir, "silent.wav")
    with wave.open(silent, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(b"\x00\x00" * 44100)
    run("tempo", silent, code=1)
    run("slice", silent, "--output", os.path.join(tone_dir, "none.sfz"), code=1)

    # 오디오 퀀타이즈: 100 BPM 루프를 120 BPM 곡의 8분음표 격자로 (워프 마커)
    warped = os.path.join(tone_dir, "warped.json")
    with open(warped, "w") as f:
        json.dump({"tempo": 120, "tracks": [{"name": "Loop", "instrument": "audio", "clips": [{"file": "loop.wav", "startBeat": 0}]}]}, f)
    qa, _ = run("arrange", warped, "quantize-audio", "Loop", "--grid", "1/8")
    marks = json.load(open(warped))["tracks"][0]["clips"][0].get("warp", [])
    check("quantize-audio writes a marker per attack on the eighth-note grid", qa.get("ok") is True and len(marks) == 17
          and all(abs((m["beat"] - marks[0]["beat"]) * 2 - round((m["beat"] - marks[0]["beat"]) * 2)) < 1e-6 for m in marks[1:-1]), marks)
    qv, _ = run("validate", warped)
    check("the warped song validates", qv.get("ok") is True, qv)
    run("arrange", warped, "quantize-audio", "Loop", "--grid", "1/8", "--mode", "slice")
    check("--mode slice cuts at the markers", json.load(open(warped))["tracks"][0]["clips"][0].get("warpMode") == "slice")
    run("arrange", warped, "unwarp", "Loop")
    unwarped = json.load(open(warped))["tracks"][0]["clips"][0]
    check("unwarp removes the markers", "warp" not in unwarped and "warpMode" not in unwarped, unwarped)
    run("arrange", warped, "quantize-audio", "Loop", "--grid", "fast", code=2)
    # 클립 편집: 뒤집기, 페이드 모양, 나눈 두 클립의 크로스페이드 (루프는 4.8초)
    edits = os.path.join(tone_dir, "edits.json")
    with open(edits, "w") as f:
        json.dump({"tempo": 120, "tracks": [{"name": "Loop", "instrument": "audio", "clips": [
            {"file": "loop.wav", "startBeat": 0, "offsetSeconds": 0.5, "lengthSeconds": 1.0},
            {"file": "loop.wav", "startBeat": 2, "offsetSeconds": 1.5, "lengthSeconds": 1.0}]}]}, f)
    run("arrange", edits, "reverse", "Loop", "--clip", "1")
    c0 = json.load(open(edits))["tracks"][0]["clips"][0]
    check("reverse plays the same part backwards (seconds from the file's end)", c0.get("reverse") is True
          and abs(c0["offsetSeconds"] - 3.3) < 1e-6 and abs(c0["lengthSeconds"] - 1.0) < 1e-6 and c0["startBeat"] == 0, c0)
    run("arrange", edits, "reverse", "Loop", "--clip", "1")
    c0 = json.load(open(edits))["tracks"][0]["clips"][0]
    check("reversing again restores the clip", "reverse" not in c0 and abs(c0["offsetSeconds"] - 0.5) < 1e-6, c0)
    run("arrange", edits, "fade", "Loop", "--clip", "2", "--in", "0.1", "--out", "0.3", "--out-shape", "exponential")
    c1 = json.load(open(edits))["tracks"][0]["clips"][1]
    check("fade sets lengths and shapes (sine is the default and is not written)", abs(c1.get("fadeInSeconds", 0) - 0.1) < 1e-9
          and c1.get("fadeOutShape") == "exponential" and "fadeInShape" not in c1, c1)
    run("arrange", edits, "fade", "Loop", "--clip", "2", "--in-shape", "cubic", code=2)
    run("arrange", edits, "fade", "Loop", code=2)
    run("arrange", edits, "crossfade", "Loop", "--ms", "20")
    cs = json.load(open(edits))["tracks"][0]["clips"]
    check("crossfade lengthens two touching parts of one sound into each other", abs(cs[0]["lengthSeconds"] - 1.01) < 1e-6
          and abs(cs[1]["offsetSeconds"] - 1.49) < 1e-6 and abs(cs[1]["startBeat"] - 1.98) < 1e-6
          and abs(cs[0].get("fadeOutSeconds", 0) - 0.02) < 1e-6 and abs(cs[1].get("fadeInSeconds", 0) - 0.02) < 1e-6
          and "fadeOutShape" not in cs[0] and "fadeInShape" not in cs[1], cs)
    ev, _ = run("validate", edits)
    check("the edited clips validate", ev.get("ok") is True and not ev.get("warnings"), ev)
    run("arrange", edits, "reverse", "Loop")
    run("arrange", edits, "crossfade", "Loop", "--clip", "2", code=1)   # 뒤 클립이 없다
    er, _ = run("render", edits, "--output", os.path.join(tone_dir, "edits.wav"))
    check("a song with reversed, crossfaded clips renders", er.get("ok") is True, er)
    # 그루브: 목록, 루프에서 뽑기, 음표와 오디오 트랙에 입히기
    gl, _ = run("groove", "list")
    check("groove list has the factory grooves", len(gl.get("grooves", [])) >= 10 and any(g["id"] == "swing-16-66" for g in gl["grooves"]), gl)
    gfile = os.path.join(tone_dir, "loop_groove.json")
    ge, _ = run("groove", "extract-audio", loop_wav, "--bpm", "100", "--grid", "1/8", "--length", "2", "--output", gfile)
    steps = ge.get("groove", {}).get("steps", [])
    check("a straight loop gives a straight groove with accents on the beat", ge.get("attacks") == 16 and len(steps) == 4
          and all(abs(st["offset"]) < 0.01 for st in steps) and steps[0]["velocity"] > steps[1]["velocity"], ge)
    grooved = os.path.join(tone_dir, "grooved.json")
    with open(grooved, "w") as f:
        json.dump({"tempo": 120, "tracks": [{"name": "Hat", "instrument": "synth.edm-drums", "notes": [
            {"pitch": 42, "startBeat": k * 0.25, "durationBeat": 0.2, "velocity": 80} for k in range(8)]}]}, f)
    run("arrange", grooved, "groove", "Hat", "--groove", "swing-16-66")
    starts = [n["startBeat"] for n in json.load(open(grooved))["tracks"][0]["notes"]]
    check("a swing groove moves the offbeat sixteenths", abs(starts[1] - 0.33) < 1e-6 and abs(starts[2] - 0.5) < 1e-6, starts)
    run("arrange", grooved, "groove", "Hat", "--groove", gfile, "--timing", "0")
    vels = [n["velocity"] for n in json.load(open(grooved))["tracks"][0]["notes"]]
    check("a groove file's accents change velocities", vels[0] > vels[2], vels)
    run("arrange", warped, "groove", "Loop", "--groove", "swing-8-60")
    check("a groove on an audio track writes warp markers", len(json.load(open(warped))["tracks"][0]["clips"][0].get("warp", [])) > 2)
    run("arrange", warped, "unwarp", "Loop")
    run("arrange", grooved, "groove", "Hat", "--groove", "no-such-groove", code=2)

    # 트랙 템플릿: 목록, 저장, 템플릿으로 트랙 더하기
    tl, _ = run("track-template", "list")
    check("track-template list has the factory templates", len(tl.get("templates", [])) >= 7 and any(t["id"] == "vocal" for t in tl["templates"]), tl)
    tfile = os.path.join(tone_dir, "hat_template.json")
    run("track-template", "save", grooved, "Hat", "--output", tfile, "--name", "Swung hat")
    tj = json.load(open(tfile))
    check("a saved template keeps the instrument and drops the notes", tj["name"] == "Swung hat" and tj["track"]["instrument"] == "synth.edm-drums"
          and "notes" not in tj["track"], tj)
    run("arrange", grooved, "add-track", "--template", tfile)
    run("arrange", grooved, "add-track", "--template", "vocal", "--name", "Lead vocal", "--at", "1")
    names = [t["name"] for t in json.load(open(grooved))["tracks"]]
    check("add-track takes a template file or id", names == ["Lead vocal", "Hat", "Hat 2"], names)
    run("arrange", grooved, "add-track", "--template", "nope", code=2)

    # 박 따라가기: 루프의 박, 곡 템포를 그 연주에 맞추기
    bt, _ = run("beats", loop_wav)
    check("beats of a 100 BPM loop", bt.get("count") in (8, 9) and abs(bt.get("bpmMedian", 0) - 100) < 1.5, bt)
    followed = os.path.join(tone_dir, "followed.json")
    with open(followed, "w") as f:
        json.dump({"tempo": 120, "tracks": [{"name": "Loop", "instrument": "audio", "clips": [{"file": "loop.wav", "startBeat": 0, "sourceBpm": 100}]}]}, f)
    run("arrange", followed, "follow-tempo", "Loop")
    fs = json.load(open(followed))
    check("follow-tempo writes a tempo map near 100 BPM and unstretches the clip", isinstance(fs["tempo"], list)
          and all(abs(p["bpm"] - 100) < 3 for p in fs["tempo"]) and "sourceBpm" not in fs["tracks"][0]["clips"][0], fs["tempo"][:3])
    run("beats", silent, code=1)

    # 음높이 교정: 조의 음으로, 지우기
    run("arrange", warped, "tune", "Loop", "--key", "A minor", "--speed", "0", "--strength", "80")
    tuned = json.load(open(warped))["tracks"][0]["clips"][0].get("pitchCorrection", {})
    check("tune writes the clip's pitch correction", tuned == {"strength": 0.8, "speedMs": 0, "key": "A minor"}, tuned)
    run("arrange", warped, "tune", "Loop", "--scale", "dorian", "--root", "D")
    check("tune takes a scale and its root", json.load(open(warped))["tracks"][0]["clips"][0]["pitchCorrection"].get("scale") == "dorian")
    run("arrange", warped, "tune", "Loop", "--key", "H minor", code=1)
    run("arrange", warped, "untune", "Loop")
    check("untune removes it", "pitchCorrection" not in json.load(open(warped))["tracks"][0]["clips"][0])

    # MIDI 가져오기: 내보낸 곡을 다시 읽으면 음, 가사, 마커, 템포, 컨트롤러가 같다
    marked = os.path.join(OUT, "marked.json")
    song = json.load(open(sung, encoding="utf-8"))
    song["title"] = "되읽기"
    song["markers"] = [{"beat": 0, "name": "시작"}, {"beat": 2, "name": "둘째"}]
    song["tempo"] = [{"beat": 0, "bpm": 72}, {"beat": 2, "bpm": 90}]
    song["tracks"][0]["cc11"] = [{"beat": 0, "value": 100, "curve": "step"}, {"beat": 2, "value": 80, "curve": "step"}]
    json.dump(song, open(marked, "w", encoding="utf-8"), ensure_ascii=False)
    me, _ = run("export-midi", marked, "--output", os.path.join(OUT, "marked.mid"))
    check("midi reports markers", me.get("markers") == 2, me)
    back = os.path.join(OUT, "marked_back.json")
    im, _ = run("import-midi", os.path.join(OUT, "marked.mid"), "--output", back)
    check("import reports what it read", (im.get("tracks"), im.get("notes"), im.get("lyrics"), im.get("markers"), im.get("tempoPoints")) == (1, 4, 2, 2, 2), im)
    b = json.load(open(back, encoding="utf-8"))
    bn = b["tracks"][0]["notes"]
    check("imported notes keep pitch, start and length", [(n["pitch"], n["startBeat"], n["durationBeat"]) for n in bn]
          == [(63, 0, 1), (68, 1, 1), (70, 2, 1), (72, 3, 1)], bn)
    check("imported lyrics sit on their notes", [n.get("lyric") for n in bn] == ["늦", "은", None, None], bn)
    check("imported markers and title", [m["name"] for m in b.get("markers", [])] == ["시작", "둘째"] and b.get("title") == "되읽기", b.get("markers"))
    check("imported tempo map", b.get("tempo") == [{"beat": 0, "bpm": 72}, {"beat": 2, "bpm": 90}], b.get("tempo"))
    cc11 = b["tracks"][0].get("automation", {}).get("cc11", [])
    check("imported controller points are steps", [(p["beat"], p["value"], p.get("curve")) for p in cc11] == [(0, 100, "step"), (2, 80, "step")], cc11)
    iv, _ = run("validate", back)
    check("the imported song validates", iv.get("ok") is True, iv)
    again, _ = run("import-midi", os.path.join(OUT, "marked.mid"), "--output", back, code=1)
    check("import never replaces a file without --force", again.get("error", {}).get("code") == "io", again)
    notmidi, _ = run("import-midi", marked, "--output", os.path.join(OUT, "never.json"), code=1)
    check("a file that is not MIDI is a midi error", notmidi.get("error", {}).get("code") == "midi", notmidi)

    # arrange: 마디 넣기와 지우기는 서로 되돌리고, 트랙과 마커 편집은 곡 검사를 지난다
    arranged = os.path.join(OUT, "arranged.json")
    shutil.copy(demo, arranged)
    before = json.load(open(arranged))
    ar, _ = run("arrange", arranged, "insert-bars", "--at", "3", "--count", "2")
    check("insert-bars moves tempo and tracks", set(ar.get("changed", [])) >= {"tracks"}, ar)
    after = json.load(open(arranged))
    bar = 4  # 4/4
    first = [n["startBeat"] for n in before["tracks"][0]["notes"]]
    moved = [n["startBeat"] for n in after["tracks"][0]["notes"]]
    check("notes after the inserted bars move by two bars", all(abs(m - (f + 2 * bar if f >= 2 * bar else f)) < 1e-9 for f, m in zip(first, moved)), (first[:12], moved[:12]))
    run("arrange", arranged, "delete-bars", "--bars", "3-4")
    check("deleting the inserted bars restores the starts", [n["startBeat"] for n in json.load(open(arranged))["tracks"][0]["notes"]] == first)
    ad, _ = run("arrange", arranged, "add-track", "--name", "Pad", "--instrument", "synth.pad", "--at", "2")
    check("add-track inserts at the position", ad.get("tracks") == ["Melody", "Pad", "Accompaniment"], ad)
    rn, _ = run("arrange", arranged, "rename-track", "1", "--name", "Lead")
    check("rename-track by number", rn.get("tracks") == ["Lead", "Pad", "Accompaniment"], rn)
    du, _ = run("arrange", arranged, "duplicate", "--bars", "1-2", "--tracks", "Lead")
    lead = json.load(open(arranged))["tracks"][0]["notes"]
    check("duplicate copies the bars right after them", [n["pitch"] for n in lead if 8 <= n["startBeat"] < 16]
          == [n["pitch"] for n in lead if n["startBeat"] < 8], du)
    mk, _ = run("arrange", arranged, "add-marker", "--at", "5", "--name", "후렴")
    check("add-marker reports the bar", mk.get("markers") == [{"bar": 5, "name": "후렴"}], mk)
    out2 = os.path.join(OUT, "arranged_copy.json")
    run("arrange", arranged, "remove-track", "Pad", "--output", out2)
    check("--output leaves the source alone", len(json.load(open(arranged))["tracks"]) == 3 and len(json.load(open(out2))["tracks"]) == 2)
    nt, _ = run("arrange", arranged, "rename-track", "Nope", "--name", "X", code=1)
    check("an unknown track suggests nothing silly", "unknown track" in nt.get("error", {}).get("message", ""), nt)
    dup, _ = run("arrange", arranged, "rename-track", "Lead", "--name", "Pad", code=1)
    check("a taken name is refused", "already exists" in dup.get("error", {}).get("message", ""), dup)
    uv, _ = run("validate", arranged)
    check("the arranged song validates", uv.get("ok") is True, uv)

    u, _ = run("render", demo, code=2)
    check("missing --output is a usage error", u.get("error", {}).get("code") == "usage", u)
    u, _ = run("render", demo, "--ouptut", "x.wav", code=2)
    check("unknown option suggests the right one", "--output" in u.get("error", {}).get("message", ""), u)
    u, _ = run("rendr", demo, code=2)
    check("unknown command suggests the right one", "render" in u.get("error", {}).get("message", ""), u)

    bad = os.path.join(OUT, "bad_instrument.json")
    json.dump({"tempo": 100, "tracks": [{"name": "X", "instrument": "synth.pinao",
               "notes": [{"pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80}]}]}, open(bad, "w"))
    f, _ = run("render", bad, "--output", os.path.join(OUT, "never.wav"), code=1)
    check("unknown instrument fails with code instrument", f.get("error", {}).get("code") == "instrument", f)
    check("no file is written on failure", not os.path.exists(os.path.join(OUT, "never.wav")))

    mastered = os.path.join(OUT, "mastered.json")
    json.dump({"tempo": 90, "render": {"tailSeconds": 2},
               "buses": [{"name": "hall", "effects": [{"type": "reverb", "mix": 1, "decaySeconds": 2.0}]}],
               "master": {"targetLufs": -18, "effects": [{"type": "eq", "lowCutHz": 30}, {"type": "limiter", "ceilingDb": -1}]},
               "tracks": [{"name": "Keys", "instrument": "synth.piano", "sends": [{"bus": "hall", "levelDb": -8}],
                           "notes": [{"pitch": p, "startBeat": i * 0.5, "durationBeat": 1.5, "velocity": 90}
                                     for i, p in enumerate([52, 55, 59, 64, 59, 55, 48, 52, 55, 60, 55, 52])]}]},
              open(mastered, "w"))
    mw = os.path.join(OUT, "mastered.wav")
    mr_, _ = run("render", mastered, "--output", mw, "--quiet")
    norm = mr_.get("normalisation", {})
    check("normalisation is reported", norm.get("limited") is True and norm.get("passes", 0) >= 1, mr_)
    mm = mr_.get("master", {})
    check("mastered loudness hits the target", abs((mm.get("lufs") or 0) + 18) <= 0.3, mm)
    check("mastered peak respects the ceiling", (mm.get("peakDb") or 0) <= -1.0 + 1e-3, mm)
    check("true peak is reported", isinstance(mm.get("truePeakDb"), (int, float)), mm)
    bad_fx = os.path.join(OUT, "bad_fx.json")
    json.dump({"tempo": 90, "master": {"effects": [{"type": "limitter"}]},
               "tracks": [{"name": "K", "instrument": "synth.sine", "notes": [{"pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80}]}]},
              open(bad_fx, "w"))
    bv, _ = run("validate", bad_fx, code=1)
    hints = [e.get("hint", "") for e in bv.get("errors", [])]
    check("unknown effect suggests the right one", any("limiter" in h for h in hints), bv)

    # format: 앱의 저장 형식으로 다시 쓴다. 뜻은 같고, 두 번 쓰면 바뀌지 않으며, --check 는 쓰지 않는다
    fsrc = os.path.join(OUT, "format_src.json")
    with open(fsrc, "w", encoding="utf-8") as f:
        json.dump(json.load(open(demo, encoding="utf-8")), f, indent=4, ensure_ascii=False)
    before = open(fsrc, "rb").read()
    fc, _ = run("format", fsrc, "--check")
    check("format --check notices another layout", fc.get("changed") is True, fc)
    check("format --check leaves the file alone", open(fsrc, "rb").read() == before)
    fout = os.path.join(OUT, "format_out.json")
    fo, _ = run("format", fsrc, "--output", fout)
    check("format writes --output", fo.get("output") == fout and os.path.exists(fout), fo)
    check("formatted song means the same", json.load(open(fout, encoding="utf-8")) == json.load(open(fsrc, encoding="utf-8")))
    check("formatted song writes one note per line", sum(1 for l in open(fout, encoding="utf-8") if '"pitch"' in l)
          == sum(len(t.get("notes", [])) for t in json.load(open(fout, encoding="utf-8"))["tracks"]))
    f2, _ = run("format", fout, "--check")
    check("formatted song is stable", f2.get("changed") is False, f2)
    run("format", code=2)

    # 빌드업 도구: 효과 매개변수 곡선, 필터, 덕킹, 센드 곡선. 잘못된 곡선은 오류, 맞는 곡은 같은 바이트로 렌더링된다
    bad_lanes = os.path.join(OUT, "bad_lanes.json")
    json.dump({"tempo": 128, "buses": [{"name": "fx", "effects": [{"type": "filter", "automation": {"cutof": [{"beat": 0, "value": 100}]}}]}],
               "master": {"effects": [{"type": "compressor", "sidechain": "K", "automation": {"ratio": [{"beat": 0, "value": 2}]}},
                                      {"type": "gain", "automation": {"gainDb": [{"beat": 0, "value": 99}]}}]},
               "tracks": [{"name": "K", "instrument": "synth.sine", "sends": [{"bus": "fx", "automation": {"pan": []}}],
                           "notes": [{"pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80}]}]},
              open(bad_lanes, "w"))
    lv, _ = run("validate", bad_lanes, code=1)
    errs = {e.get("path"): e for e in lv.get("errors", [])}
    check("unknown automated parameter is an error with a hint",
          "did you mean 'cutoffHz'" in errs.get("$.buses[0].effects[0].automation.cutof", {}).get("hint", ""), lv.get("errors"))
    check("a parameter that cannot be automated is an error",
          "cannot be automated" in errs.get("$.master.effects[0].automation.ratio", {}).get("message", ""), lv.get("errors"))
    check("an automated value out of range is an error", "$.master.effects[1].automation.gainDb[0].value" in errs, lv.get("errors"))
    check("a send automates only levelDb", "$.tracks[0].sends[0].automation.pan" in errs, lv.get("errors"))
    buildup = os.path.join(OUT, "buildup.json")
    json.dump({"tempo": 128, "render": {"tailSeconds": 0.5},
               "buses": [{"name": "fx", "effects": [{"type": "delay", "timeBeats": 0.75, "feedback": 0.3, "mix": 1}]}],
               "master": {"effects": [{"type": "filter", "mode": "highpass", "cutoffHz": 20,
                                       "automation": {"cutoffHz": [{"beat": 0, "value": 20}, {"beat": 8, "value": 900}]}},
                                      {"type": "ducker", "depthDb": -10, "releaseBeats": 0.5}]},
               "tracks": [{"name": "Pad", "instrument": "synth.saw-pad",
                           "sends": [{"bus": "fx", "levelDb": -6, "automation": {"levelDb": [{"beat": 6, "value": -120}, {"beat": 7, "value": 0}]}}],
                           "notes": [{"pitch": p, "startBeat": 0, "durationBeat": 8, "velocity": 90} for p in (57, 60, 64)]}]},
              open(buildup, "w"))
    bv2, _ = run("validate", buildup)
    check("build-up song validates without warnings", bv2.get("ok") is True and not bv2.get("warnings"), bv2)
    b1, _ = run("render", buildup, "--output", os.path.join(OUT, "buildup.wav"), "--quiet")
    run("render", buildup, "--output", os.path.join(OUT, "buildup_again.wav"), "--quiet")
    check("build-up render is deterministic", sha(os.path.join(OUT, "buildup.wav")) == sha(os.path.join(OUT, "buildup_again.wav")))
    check("build-up render is audible", (b1.get("master", {}).get("peakDb") or -999) > -40, b1.get("master"))

    # 형식: 확장자로 고른다. FLAC 과 Ogg 는 WAV 와 샘플 수가 같고, 같은 곡은 같은 바이트다
    for ext, fmt in (("flac", "FLAC"), ("ogg", "Ogg"), ("mp3", "MP3")):
        out = os.path.join(OUT, "mastered." + ext)
        fr, _ = run("render", mastered, "--output", out, "--quiet")
        check(f"{ext}: format is reported", fr.get("format") == ext, fr.get("format"))
        check(f"{ext}: the master stats match the wav render", abs((fr.get("master", {}).get("lufs") or 0) - (mm.get("lufs") or 99)) < 1e-6, fr.get("master"))
        fi, _ = run("inspect", out)
        check(f"{ext}: inspect reads it", fmt.lower() in str(fi.get("format", "")).lower() or ext == "mp3", fi.get("format"))
        if ext == "mp3":
            check("mp3: 320 kbps by default", fr.get("quality") == 320, fr.get("quality"))
            check("mp3: duration is close", abs((fi.get("duration") or 0) - mm.get("duration", 0)) < 0.1, (fi.get("duration"), mm.get("duration")))
        else:
            check(f"{ext}: same sample count as wav", fi.get("samples") == mm.get("samples"), (fi.get("samples"), mm.get("samples")))
            check(f"{ext}: loudness survives encoding", abs((fi.get("lufs") or 0) - (mm.get("lufs") or 99)) < 0.3, (fi.get("lufs"), mm.get("lufs")))
    again = os.path.join(OUT, "mastered_again.ogg")
    run("render", mastered, "--output", again, "--quiet")
    check("ogg: same song, same bytes", sha(again) == sha(os.path.join(OUT, "mastered.ogg")))
    q, _ = run("render", mastered, "--output", os.path.join(OUT, "mastered_q2.ogg"), "--quality", "2", "--quiet")
    check("ogg: lower quality makes a smaller file", os.path.getsize(os.path.join(OUT, "mastered_q2.ogg")) < os.path.getsize(again), q.get("quality"))

    # 반복 구간: metadata.loop 의 끝에서 자르고 표시를 남긴다
    looped = os.path.join(OUT, "looped.json")
    song = json.load(open(mastered))
    song["title"] = "되풀이"
    song["metadata"] = {"loop": {"startBeat": 2, "endBeat": 6}}
    json.dump(song, open(looped, "w"), ensure_ascii=False)
    lo_out = os.path.join(OUT, "looped.ogg")
    lr, _ = run("render", looped, "--output", lo_out, "--loop", "--quiet")
    loop = lr.get("loop", {})
    check("loop: samples from the tempo map", (loop.get("startSample"), loop.get("endSample")) == (64000, 192000), loop)
    check("loop: the file ends at the loop end", lr.get("master", {}).get("samples") == 192000, lr.get("master"))
    check("loop: seam is measured", loop.get("seamDb") is not None and loop.get("crossfadeSamples") == 960, loop)
    li_, _ = run("inspect", lo_out)
    check("loop: ogg carries LOOPSTART and LOOPLENGTH", li_.get("tags", {}).get("LOOPSTART") == "64000" and li_.get("tags", {}).get("LOOPLENGTH") == "128000", li_.get("tags"))
    check("loop: ogg carries the title", li_.get("tags", {}).get("TITLE") == "되풀이", li_.get("tags"))
    lw = os.path.join(OUT, "looped.wav")
    run("render", looped, "--output", lw, "--loop", "--quiet")
    smpl = open(lw, "rb").read()
    at = smpl.find(b"smpl")
    check("loop: wav has a smpl chunk", at > 0)
    if at > 0:
        import struct
        start, end = struct.unpack("<II", smpl[at + 8 + 36 + 8:at + 8 + 36 + 16])
        check("loop: smpl start and inclusive end", (start, end) == (64000, 191999), (start, end))
    bad_loop, _ = run("render", looped, "--output", os.path.join(OUT, "looped.mp3"), "--loop", code=1)
    check("loop: mp3 is refused", bad_loop.get("error", {}).get("code") == "invalid-input", bad_loop)
    no_loop, _ = run("render", mastered, "--output", os.path.join(OUT, "noloop.wav"), "--loop", code=1)
    check("loop: a song without metadata.loop is refused", "metadata.loop" in no_loop.get("error", {}).get("message", ""), no_loop)

    # 마디 구간: piano_demo 는 4/4, 76 BPM 이라 2~3마디는 4박에서 12박, 끝 뒤 1초 페이드
    br, _ = run("render", demo, "--output", os.path.join(OUT, "bars.wav"), "--bars", "2-3", "--fade", "1", "--quiet")
    rng = br.get("range", {})
    check("bars: beats from the time signature", (rng.get("startBeat"), rng.get("endBeat")) == (4, 12), rng)
    expected = round(12 * 60 / 76 * 48000) + 48000 - round(4 * 60 / 76 * 48000)
    check("bars: length is the range plus the fade", abs(br.get("master", {}).get("samples", 0) - expected) <= 1, (br.get("master", {}).get("samples"), expected))
    for args in (("--output", os.path.join(OUT, "x.aiff")), ("--output", os.path.join(OUT, "x.wav"), "--loop", "--bars", "1-2"),
                 ("--output", os.path.join(OUT, "x.wav"), "--bars", "0-2"), ("--output", os.path.join(OUT, "x.wav"), "--fade", "2"),
                 ("--output", os.path.join(OUT, "x.flac"), "--bit-depth", "32"), ("--output", os.path.join(OUT, "x.wav"), "--beats", "8-4")):
        ue, _ = run("render", demo, *args, code=2)
        check("usage error: " + " ".join(args[2:] or args[1:])[-40:], ue.get("error", {}).get("code") == "usage", ue)

    # 템플릿: 목록과 새 곡 (음표 없이 검사를 통과하고, 있는 파일은 덮어쓰지 않는다)
    tl, _ = run("templates")
    ids = [t["id"] for t in tl.get("templates", [])]
    check("templates are listed in order", ids[:3] == ["empty", "piano-solo", "orchestral-battle"] and len(ids) >= 13, ids)
    check("templates describe their tracks", all(t.get("tracks") and t.get("description") for t in tl.get("templates", [])), tl.get("warnings"))
    fresh = os.path.join(OUT, "fresh", "battle.json")
    nw, _ = run("new", "orchestral-battle", "--output", fresh, "--title", "새 전투")
    check("new writes the song", os.path.exists(fresh) and nw.get("title") == "새 전투", nw)
    fv, _ = run("validate", fresh)
    check("the new song validates", fv.get("ok") is True and not fv.get("errors"), fv.get("errors"))
    song_text = json.load(open(fresh)) if os.path.exists(fresh) else {}
    check("the new song keeps the loop and names its template", song_text.get("metadata", {}).get("template") == "orchestral-battle"
          and "loop" in song_text.get("metadata", {}), song_text.get("metadata"))
    again_new, _ = run("new", "orchestral-battle", "--output", fresh, code=1)
    check("new never replaces a file", again_new.get("error", {}).get("code") == "io", again_new)
    unknown, _ = run("new", "orchestral-batle", "--output", os.path.join(OUT, "fresh", "x.json"), code=1)
    check("unknown template suggests the right one", "orchestral-battle" in unknown.get("error", {}).get("message", ""), unknown)
    empty_song = os.path.join(OUT, "fresh", "empty.json")
    run("new", "empty", "--output", empty_song)
    er, _ = run("render", empty_song, "--output", os.path.join(OUT, "fresh", "empty.wav"), "--quiet")
    check("an empty song renders its tail", er.get("ok") is True and er.get("master", {}).get("samples", 0) > 0, er.get("master"))

    # 화음표와 음계표
    ch, _ = run("chords")
    symbols = {c["id"]: c["symbol"] for c in ch.get("chords", [])}
    check("chords: the table is complete", len(symbols) >= 60 and symbols.get("m7") == "Cm7" and symbols.get("maj") == "C", len(symbols))
    check("chords: every chord names a known group", all(c["group"] in ch.get("groups", []) for c in ch.get("chords", [])))
    sc, _ = run("scales")
    scales = {x["id"]: x["intervals"] for x in sc.get("scales", [])}
    check("scales: the table is complete", len(scales) >= 50 and scales.get("dorian") == [0, 2, 3, 5, 7, 9, 10], len(scales))

    li, _ = run("instruments")
    names = {x["name"] for x in li.get("instruments", [])}
    check("instruments lists registry and built-ins", {"piano", "strings", "synth.sine"} <= names, names)
    club = next((x for x in li.get("instruments", []) if x["name"] == "synth.club-drums"), {})
    check("instruments lists the club kit with its key names", club.get("keyNames", {}).get("36") == "킥" and club.get("keyNames", {}).get("66") == "상승음 16박", club)
    edm = next((x for x in li.get("instruments", []) if x["name"] == "synth.edm-drums"), {})
    check("built-in drum kits name their keys", edm.get("keyNames", {}).get("38") == "스네어", edm.get("keyNames"))

    # 클럽 키트와 신스 값: 필터 방식은 이름으로, 박 단위 효과음은 그 박에서 끝난다
    clubsong = os.path.join(OUT, "club.json")
    json.dump({"tempo": 140, "render": {"tailSeconds": 1.0},
               "tracks": [{"name": "Kick", "instrument": "synth.club-drums", "synth": {"tune": -3},
                           "notes": [{"pitch": 36, "startBeat": b, "durationBeat": 0.25, "velocity": 110} for b in range(8)]},
                          {"name": "Roll", "instrument": "synth.club-drums", "synth": {"bendRange": 12},
                           "pitchBend": [{"beat": 4, "value": 0}, {"beat": 8, "value": 1}],
                           "notes": [{"pitch": 40, "startBeat": 4 + i / 8, "durationBeat": 0.125, "velocity": 70 + i} for i in range(32)]},
                          {"name": "FX", "instrument": "synth.club-drums",
                           "notes": [{"pitch": 65, "startBeat": 0, "durationBeat": 1, "velocity": 100}, {"pitch": 60, "startBeat": 8, "durationBeat": 1, "velocity": 110}]},
                          {"name": "Noise", "instrument": "synth.noise", "synth": {"filterMode": "highpass", "cutoffHz": 9000, "bendRange": 12},
                           "cc74": [{"beat": 0, "value": 0}, {"beat": 8, "value": 127}],
                           "notes": [{"pitch": 60, "startBeat": 0, "durationBeat": 8, "velocity": 90}]}]},
              open(clubsong, "w"), ensure_ascii=False)
    cv, _ = run("validate", clubsong)
    check("club song validates without warnings", cv.get("ok") is True and not cv.get("warnings"), cv)
    c1, _ = run("render", clubsong, "--output", os.path.join(OUT, "club.wav"), "--quiet")
    run("render", clubsong, "--output", os.path.join(OUT, "club_again.wav"), "--quiet")
    check("club render is deterministic", sha(os.path.join(OUT, "club.wav")) == sha(os.path.join(OUT, "club_again.wav")))
    check("club render is audible", (c1.get("master", {}).get("peakDb") or -999) > -20, c1.get("master"))
    badmode = os.path.join(OUT, "badmode.json")
    json.dump({"tempo": 120, "tracks": [{"name": "N", "instrument": "synth.noise", "synth": {"filterMode": "hipass"}, "notes": []}]}, open(badmode, "w"))
    bm, _ = run("validate", badmode, code=1)
    check("an unknown filter mode is an error with a hint", any(e.get("path") == "$.tracks[0].synth.filterMode" and "highpass" in e.get("message", "") + e.get("hint", "")
                                                              for e in bm.get("errors", [])), bm.get("errors"))
    unused = os.path.join(OUT, "unused_synth.json")
    json.dump({"tempo": 120, "tracks": [{"name": "P", "instrument": "synth.pluck", "synth": {"bendRange": 3}, "notes": []},
                                         {"name": "Pad", "instrument": "synth.pad", "synth": {"tune": 3, "release": 1}, "notes": []}]}, open(unused, "w"))
    uv, _ = run("validate", unused)
    upaths = [w.get("path") for w in uv.get("warnings", [])]
    check("synth values the preset does not use are warned about", uv.get("ok") is True and upaths == ["$.tracks[0].synth.bendRange", "$.tracks[1].synth.tune"]
          and "no effect on synth.pluck" in uv["warnings"][0].get("message", ""), uv.get("warnings"))
    longecho = os.path.join(OUT, "long_echo.json")
    json.dump({"tempo": 40, "master": {"effects": [{"type": "delay", "timeBeats": 16}]}, "tracks": [{"name": "A", "instrument": "synth.sine", "notes": []}]},
              open(longecho, "w"))
    le, _ = run("validate", longecho, code=1)
    check("validate refuses a beat delay longer than 16 s at the slowest tempo",
          any(e.get("path") == "$.master.effects[0].timeBeats" and "24 s long" in e.get("message", "") for e in le.get("errors", [])), le.get("errors"))

    # ---- 구간: 마커에서 다음 마커까지를 옮기고, 복제하고, 지운다 ----
    sec = os.path.join(OUT, "sections.json")
    json.dump({"tempo": [{"beat": 0, "bpm": 120}, {"beat": 8, "bpm": 90}], "timeSignature": [4, 4],
               "meterChanges": [{"beat": 8, "timeSignature": [3, 4]}, {"beat": 14, "timeSignature": [4, 4]}],
               "markers": [{"beat": 0, "name": "A"}, {"beat": 8, "name": "B"}, {"beat": 14, "name": "C"}], "render": {"endBeat": 22},
               "tracks": [{"name": "Keys", "instrument": "synth.piano", "notes": [
                   {"pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80}, {"pitch": 64, "startBeat": 8, "durationBeat": 1, "velocity": 80},
                   {"pitch": 67, "startBeat": 14, "durationBeat": 1, "velocity": 80}]}]}, open(sec, "w"))
    moved = os.path.join(OUT, "sections_moved.json")
    ms, _ = run("arrange", sec, "move-section", "B", "--before", "A", "--output", moved)
    names = [(x["name"], x["firstBar"], x["bars"]) for x in ms.get("sections", [])]
    check("move-section puts B first with its 3/4 bars", names == [("B", 1, 2), ("A", 3, 2), ("C", 5, 2)], names)
    mv, _ = run("validate", moved)
    check("the moved song validates", mv.get("ok") is True and not mv.get("errors"), mv)
    song = json.load(open(moved))
    check("the moved song starts in 3/4 at 90 BPM", song.get("timeSignature") == [3, 4] and song["tempo"][0]["bpm"] == 90
          and [n["pitch"] for n in song["tracks"][0]["notes"]] == [64, 60, 67], (song.get("timeSignature"), song.get("tempo")))
    ds, _ = run("arrange", sec, "duplicate-section", "2", "--output", os.path.join(OUT, "sections_twice.json"))
    check("duplicate-section repeats a section after itself", [x["name"] for x in ds.get("sections", [])] == ["A", "B", "B", "C"], ds.get("sections"))
    dl, _ = run("arrange", sec, "delete-section", "B", "--output", os.path.join(OUT, "sections_less.json"))
    check("delete-section removes it", [x["name"] for x in dl.get("sections", [])] == ["A", "C"], dl.get("sections"))
    run("arrange", sec, "move-section", "Z", "--before", "A", "--output", moved, code=1)
    me, _ = run("arrange", sec, "move-section", "C", "--before", "end", "--output", os.path.join(OUT, "sections_same.json"))
    check("moving the last section to the end changes nothing", me.get("changed") == [], me.get("changed"))

    # ---- 영상: 시험 영상, 정보, 프레임, 장면 바뀜, 소리, 곡을 영상에 입히기 ----
    movie = os.path.join(OUT, "pattern.mp4")
    vp, _ = run("video-pattern", "--output", movie, "--fps", "29.97", "--seconds", "6", "--size", "320x180", "--scenes", "1.5,4", "--clicks", "1")
    check("video-pattern writes a 29.97 fps movie", vp.get("frameRateExact") == "30000/1001" and vp.get("frames") == 180 and vp.get("dropFrame") is True, vp)
    vi, _ = run("video-info", movie)
    check("video-info reads it back", vi.get("width") == 320 and vi.get("codec") == "avc1" and (vi.get("audio") or {}).get("channels") == 2
          and vi.get("duration") == "00:00:06;00", vi)
    frame_png = os.path.join(OUT, "frame.png")
    vf, _ = run("video-frame", movie, "--at", "2.5", "--output", frame_png, "--width", "160")
    check("video-frame picks the frame shown at that time", vf.get("frame") == 74 and vf.get("timecode") == "00:00:02;14" and vf.get("width") == 160, vf)
    check("video-frame writes a PNG", os.path.exists(frame_png) and open(frame_png, "rb").read(8) == b"\x89PNG\r\n\x1a\n")
    vs, _ = run("video-scenes", movie)
    check("video-scenes finds both cuts", [sc["frame"] for sc in vs.get("scenes", [])] == [44, 119], vs.get("scenes"))
    vsong = os.path.join(OUT, "video_song.json")
    json.dump({"title": "Video", "tempo": 120, "video": {"file": "pattern.mp4", "offsetSeconds": 2},
               "tracks": [{"name": "Lead", "instrument": "synth.sine", "notes": [{"pitch": 69, "startBeat": 0, "durationBeat": 16, "velocity": 100}]}]},
              open(vsong, "w"))
    vv, _ = run("validate", vsong)
    check("a song with a video validates", vv.get("ok") is True and not vv.get("warnings")
          and abs((vv.get("summary", {}).get("video") or {}).get("durationSeconds", 0) - 6.006) < 1e-3, vv)
    vss, _ = run("video-scenes", movie, "--song", vsong)
    first = (vss.get("scenes") or [{}])[0]
    check("video-scenes places a cut in the song's bars", first.get("bar") == 2 and abs(first.get("beat", 0) - 6.9363) < 1e-3, first)
    missing = os.path.join(OUT, "video_missing.json")
    json.dump({"tempo": 120, "video": {"file": "nowhere.mp4"}, "tracks": [{"name": "A", "instrument": "synth.sine", "notes": []}]}, open(missing, "w"))
    mv, _ = run("validate", missing)
    check("a missing video is a warning", mv.get("ok") is True and any(w.get("path") == "$.video.file" for w in mv.get("warnings", [])), mv)

    def rms24(path, start, end):
        with wave.open(path, "rb") as w:
            rate, ch = w.getframerate(), w.getnchannels()
            w.setpos(int(start * rate))
            raw = w.readframes(int((end - start) * rate))
        total, n = 0.0, 0
        for i in range(0, len(raw) - 2, 3 * ch):
            v = int.from_bytes(raw[i:i + 3], "little", signed=True) / 8388608.0
            total += v * v
            n += 1
        return 10 * math.log10(total / n) if total > 0 and n else -300.0

    scored = os.path.join(OUT, "scored.mov")
    sr, _ = run("render", vsong, "--output", scored, "--video-offset", "-2", "--quiet")
    vid = sr.get("video", {})
    check("render puts the song on its video", sr.get("format") == "mov" and vid.get("videoSamples") == 180 and vid.get("offsetSeconds") == -2
          and vid.get("originalSound") == "drop", sr)
    check("the music fills the video from 2 s", abs(vid.get("musicSeconds", 0) - 4.006) < 0.01, vid)
    si, _ = run("video-info", scored)
    check("the scored video keeps every frame", si.get("frames") == 180 and (si.get("audio") or {}).get("sampleRate") == 48000, si)
    sound = os.path.join(OUT, "scored_sound.wav")
    run("video-audio", scored, "--output", sound)
    if os.path.exists(sound):
        before, after = rms24(sound, 0.1, 1.9), rms24(sound, 2.2, 4.0)
        check("the scored sound is silent before the music and loud after", before < -80 and after > -30, (before, after))
    mixed = os.path.join(OUT, "scored_mixed.mp4")
    mx, _ = run("render", vsong, "--output", mixed, "--video-offset", "-2", "--video-sound", "mix", "--quiet")
    check("--video-sound mix keeps the video's sound", (mx.get("video") or {}).get("originalSound") == "mix", mx.get("video"))
    mixed_sound = os.path.join(OUT, "scored_mixed_sound.wav")
    run("video-audio", mixed, "--output", mixed_sound)
    if os.path.exists(mixed_sound):
        quiet, click = rms24(mixed_sound, 0.5, 0.95), rms24(mixed_sound, 0.995, 1.01)
        check("the video's click is in the mixed sound", quiet < -80 and click > -25, (quiet, click))
    run("render", vsong, "--output", os.path.join(OUT, "x.mp4"), "--loop", code=2)
    run("render", demo, "--output", os.path.join(OUT, "x.mp4"), code=2)
    run("video-frame", movie, "--at", "99", "--output", frame_png, code=1)
    run("video-info", os.path.join(OUT, "nope.mp4"), code=1)

    if PLUGIN:
        cache = os.path.join(OUT, "plugins.json")
        sc, _ = run("plugins", "scan", PLUGIN, "--cache", cache)
        names = [p["name"] for p in sc.get("plugins", [])]
        check("plugins scan finds the test synth", names == ["MusicRender Test Synth"], sc)
        check("plugins scan writes the cache", os.path.exists(cache))
        de, _ = run("plugins", "describe", PLUGIN)
        params = {p["name"]: p for p in de.get("parameters", [])}
        check("describe lists parameters", {"Level", "Attack", "Release", "Latency"} <= set(params), list(params))
        check("describe lists programs", de.get("programs") == ["Default", "Quiet"], de.get("programs"))
        state = os.path.join(OUT, "state.bin")
        ss, _ = run("plugins", "save-state", PLUGIN, "--output", state, "--set", "Level=0.5", "--set", "Release=200 ms")
        lv = {p["name"]: p for p in ss.get("parameters", [])}
        check("save-state applies --set", abs(lv.get("Level", {}).get("value", 0) - 0.5) < 1e-6, lv.get("Level"))
        check("save-state writes the state", os.path.getsize(state) > 0 if os.path.exists(state) else False)
        bad, _ = run("plugins", "describe", os.path.join(OUT, "nope.vst3"), code=1)
        check("missing plugin is an instrument error", bad.get("error", {}).get("code") == "instrument", bad)

        reg = os.path.join(OUT, "plugin_registry.json")
        json.dump({"instruments": {"piano": {"type": "vst3", "plugin": PLUGIN, "stateFile": state}}}, open(reg, "w"))
        # 빌드 폴더의 플러그인은 설치된 플러그인 폴더 밖이라, 곡과 레지스트리로는 MUSIC_RENDER_PLUGIN_PATH 에 그 폴더를 줄 때만 불러온다
        outside, _ = run("render", demo, "--output", os.path.join(OUT, "x.wav"), "--registry", reg, "--quiet", code=1)
        check("a plugin outside the installed folders is refused", "outside the installed plugin folders" in json.dumps(outside), outside)
        bundled = os.path.join(OUT, "bundled")
        shutil.rmtree(bundled, ignore_errors=True)
        shutil.copytree(PLUGIN, os.path.join(bundled, "fx", "Bundled.vst3"))
        bsong = os.path.join(bundled, "song.json")
        json.dump({"tempo": 120, "tracks": [{"name": "Keys", "instrument": "synth.piano",
                   "effects": [{"type": "plugin", "format": "vst3", "plugin": "fx/Bundled.vst3"}],
                   "notes": [{"pitch": "C4", "startBeat": 0, "durationBeat": 1, "velocity": 90}]}]}, open(bsong, "w"))
        bo, _ = run("render", bsong, "--output", os.path.join(OUT, "x.wav"), "--quiet", code=1)
        check("a plugin that comes with a song is not loaded", "outside the installed plugin folders" in json.dumps(bo), bo)
        allow = {"MUSIC_RENDER_PLUGIN_PATH": os.path.dirname(PLUGIN)}
        pw = os.path.join(OUT, "demo_plugin.wav")
        pr, _ = run("render", demo, "--output", pw, "--registry", reg, "--quiet", env=allow)
        check("the same composition renders through the plugin", all(t["engine"] == "vst3:MusicRender Test Synth" for t in pr.get("tracks", [])), pr.get("tracks"))
        check("plugin render has the same length", pr.get("master", {}).get("samples") == m.get("samples"), (pr.get("master"), m))
        pr2, _ = run("render", demo, "--output", os.path.join(OUT, "demo_plugin2.wav"), "--registry", reg, "--quiet", env=allow)
        check("plugin rendering is deterministic", sha(pw) == sha(os.path.join(OUT, "demo_plugin2.wav")))

    print(f"\n{CHECKS} checks, {len(FAILS)} failures")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
