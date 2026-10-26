"""Builds data/audio from the raw ElevenLabs downloads.

usage: build_bank.py <raw_dir> <out_dir>

raw_dir holds sfx/<key>_<n>.mp3 and {de,en}/<key>_<n>.mp3 (see fetch_generations.py).
Sound effects become mono 32 kHz one-shots (leading silence trimmed, tail faded, peak normalised),
ambience loops get a crossfaded seam, bridge reports get a sound-powered-telephone
band-pass and light compression. Everything is written as 16-bit PCM WAV, which is what the
XAudio2 loader expects, together with bank.json.
"""
import json
import os
import re
import subprocess
import sys
import wave

import numpy as np

SFX_RATE = 32000
LOOP_RATE = 22050
VOICE_RATE = 16000
MAX_VARIANTS = 3

# key -> (max length s, peak dBFS, preferred minimum length s)
ONE_SHOTS = {
    "gun_large": (4.0, -1.0, 1.0),
    "gun_medium": (3.0, -1.0, 0.6),
    "gun_small": (2.5, -2.0, 0.4),
    "gun_distant": (4.0, -3.0, 1.0),
    "splash_large": (3.0, -2.0, 0.8),
    "splash_small": (2.0, -4.0, 0.5),
    "hit_metal": (2.5, -1.0, 0.5),
    "explosion_medium": (4.0, -1.0, 0.7),
    "torpedo_explosion": (6.0, -1.0, 2.0),
    "magazine_explosion": (9.0, -0.5, 3.0),
    "starshell_pop": (2.5, -4.0, 0.3),
    "torpedo_launch": (3.0, -3.0, 0.5),
    "searchlight_on": (2.0, -6.0, 0.2),
    "shell_whistle": (3.0, -3.0, 0.8),
    "sinking": (6.0, -2.0, 1.5),
    "alarm_bell": (4.0, -6.0, 1.5),
    "ui_click": (0.5, -10.0, 0.05),
    "ui_telegraph": (2.0, -8.0, 0.4),
}
# raw key -> (bank id, RMS target dBFS, channels)
LOOPS = {
    "loop_amb_sea": ("amb_sea", -20.0, 2),
    "loop_amb_wind": ("amb_wind", -22.0, 2),
    "loop_fire": ("fire_loop", -18.0, 1),
}
VOICE_FILTER = "highpass=f=320,highpass=f=320,lowpass=f=3400,lowpass=f=3400,acompressor=threshold=-20dB:ratio=3:attack=4:release=90:makeup=2"


def decode(path, rate, channels, af=None):
    cmd = ["ffmpeg", "-v", "error", "-i", path]
    if af:
        cmd += ["-af", af]
    cmd += ["-ac", str(channels), "-ar", str(rate), "-f", "f32le", "-"]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, channels).astype(np.float64)


def write_wav(path, x, rate):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pcm = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def db(v):
    return 20.0 * np.log10(max(v, 1e-12))


def envelope(x, rate, win=0.01):
    n = max(1, int(rate * win))
    m = np.abs(x).max(axis=1)
    pad = (-len(m)) % n
    return np.pad(m, (0, pad)).reshape(-1, n).max(axis=1), n


def trim(x, rate, start_db, end_db, pre=0.004, post=0.06):
    """Cut leading/trailing parts quieter than the given level below the clip's peak."""
    env, n = envelope(x, rate)
    peak = env.max()
    if peak <= 0:
        return x[:0]
    above_s = np.nonzero(env > peak * 10 ** (start_db / 20))[0]
    above_e = np.nonzero(env > peak * 10 ** (end_db / 20))[0]
    a = max(0, above_s[0] * n - int(pre * rate))
    b = min(len(x), (above_e[-1] + 1) * n + int(post * rate))
    return x[a:b]


def fade(x, rate, fin, fout):
    x = x.copy()
    i, o = min(len(x), int(fin * rate)), min(len(x), int(fout * rate))
    if i:
        x[:i] *= np.linspace(0, 1, i)[:, None]
    if o:
        x[-o:] *= np.linspace(1, 0, o)[:, None] ** 2
    return x


def peak_norm(x, target_db):
    p = np.abs(x).max()
    return x * (10 ** (target_db / 20) / p) if p > 0 else x


def active_rms(x, rate):
    env, _ = envelope(x, rate, 0.05)
    loud = env > env.max() * 0.1
    seg = np.sqrt(np.mean(x**2))
    return seg, float(loud.mean())


def takes(raw_dir, cat):
    out = {}
    d = os.path.join(raw_dir, cat)
    for f in sorted(os.listdir(d)):
        m = re.match(r"(.+)_(\d+)\.mp3$", f)
        if m:
            out.setdefault(m.group(1), []).append(os.path.join(d, f))
    return out


def build_one_shots(raw_dir, out_dir, bank, report):
    for key, files in sorted(takes(raw_dir, "sfx").items()):
        if key not in ONE_SHOTS:
            continue
        max_len, peak_db, min_len = ONE_SHOTS[key]
        cands = []
        for f in files:
            x = decode(f, SFX_RATE, 1)
            x = trim(x, SFX_RATE, -40.0, -48.0)
            if len(x) < SFX_RATE * (0.04 if key == "ui_click" else 0.15):
                report.append(f"  drop {os.path.basename(f)}: too short after trimming")
                continue
            x = x[: int(max_len * SFX_RATE)]
            x = fade(x, SFX_RATE, 0.002, min(0.25, len(x) / SFX_RATE * 0.3))
            rms, busy = active_rms(x, SFX_RATE)
            short = 10.0 if len(x) < min_len * SFX_RATE else 0.0
            cands.append((f, peak_norm(x, peak_db), db(rms), busy, short))
        # Prefer takes with a full body (high RMS relative to the peak) over thin, clipped or mostly silent ones.
        cands.sort(key=lambda c: c[2] + 6.0 * c[3] - c[4], reverse=True)
        bank["sounds"][key] = []
        for i, (f, x, rms, busy, _) in enumerate(cands[:MAX_VARIANTS]):
            rel = f"sfx/{key}_{i}.wav"
            write_wav(os.path.join(out_dir, rel), x, SFX_RATE)
            bank["sounds"][key].append(rel)
            report.append(f"  {rel:32s} <- {os.path.basename(f):24s} {len(x) / SFX_RATE:5.2f}s rms {rms:6.1f} dB busy {busy:.2f}")
        for f, *_ in cands[MAX_VARIANTS:]:
            report.append(f"  skip {os.path.basename(f)}")


def build_loops(raw_dir, out_dir, bank, report):
    for key, files in sorted(takes(raw_dir, "sfx").items()):
        if key not in LOOPS:
            continue
        name, rms_db, channels = LOOPS[key]
        best = None
        for f in files:
            x = decode(f, LOOP_RATE, channels)
            env, _ = envelope(x, LOOP_RATE, 0.25)
            steadiness = float(np.std(20 * np.log10(np.maximum(env, 1e-6))))
            report.append(f"  {os.path.basename(f)}: level spread {steadiness:.2f} dB")
            if best is None or steadiness < best[0]:
                best = (steadiness, f, x)
        _, f, x = best
        # Equal-power crossfade of the tail into the head so the seam cannot click.
        xf = int(0.15 * LOOP_RATE)
        w = np.sin(np.linspace(0, np.pi / 2, xf))[:, None]
        y = x[: len(x) - xf].copy()
        y[:xf] = x[:xf] * w + x[len(x) - xf :] * np.cos(np.linspace(0, np.pi / 2, xf))[:, None]
        y *= 10 ** (rms_db / 20) / max(np.sqrt(np.mean(y**2)), 1e-9)
        y = np.clip(y, -0.98, 0.98)
        rel = f"sfx/{name}.wav"
        write_wav(os.path.join(out_dir, rel), y, LOOP_RATE)
        bank["sounds"][name] = [rel]
        report.append(f"  {rel:32s} <- {os.path.basename(f)} {len(y) / LOOP_RATE:.1f}s")


def build_voices(raw_dir, out_dir, bank, report):
    for lang in ("de", "en"):
        table = bank["voices"].setdefault(lang, {})
        for key, files in sorted(takes(raw_dir, lang).items()):
            table[key] = []
            for i, f in enumerate(files):
                x = decode(f, VOICE_RATE, 1, VOICE_FILTER)
                x = trim(x, VOICE_RATE, -35.0, -40.0, pre=0.03, post=0.12)
                x = fade(peak_norm(x, -2.0), VOICE_RATE, 0.01, 0.08)
                rel = f"voice/{lang}/{key}_{i}.wav"
                write_wav(os.path.join(out_dir, rel), x, VOICE_RATE)
                table[key].append(rel)
            report.append(f"  {lang}/{key}: {len(table[key])} takes")


def main():
    raw_dir, out_dir = sys.argv[1:3]
    bank = {"sounds": {}, "voices": {}}
    report = []
    build_one_shots(raw_dir, out_dir, bank, report)
    build_loops(raw_dir, out_dir, bank, report)
    build_voices(raw_dir, out_dir, bank, report)
    with open(os.path.join(out_dir, "bank.json"), "w", encoding="utf-8") as fh:
        json.dump(bank, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print("\n".join(report))


if __name__ == "__main__":
    main()
