"""Turn a capture into numbers: which note it was, what the partials do, where
the energy sits, and how wide it is.

  python analyze.py take.wav
  python analyze.py serum.wav forge.wav          # side by side with deltas
  python analyze.py take.wav --peaks 12          # strongest peaks, for sidebands
  python analyze.py take.wav --window 0.5:2.0    # pick the segment by hand

The note is located by its envelope, so a long take with one note in the middle
of it analyses correctly - record long rather than guessing a window.
"""

import argparse
import math
import os
import shutil
import subprocess
import sys
import wave

import numpy as np

BANDS = [(20, 100), (100, 500), (500, 2000), (2000, 8000), (8000, 20000)]
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


# ---------------------------------------------------------------- loading

def _decode_with_ffmpeg(path):
    exe = shutil.which("ffmpeg") or os.path.expandvars(
        r"%USERPROFILE%\Downloads\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe"
    )
    probe = subprocess.run(
        [exe, "-hide_banner", "-i", path], capture_output=True, text=True
    ).stderr
    sr, ch = 48000, 2
    for line in probe.splitlines():
        if "Audio:" in line:
            for part in line.split(","):
                part = part.strip()
                if part.endswith("Hz"):
                    sr = int(part[:-2].strip())
                elif part == "mono":
                    ch = 1
    raw = subprocess.run(
        [exe, "-v", "quiet", "-i", path, "-f", "f32le", "-acodec", "pcm_f32le", "-"],
        capture_output=True,
    ).stdout
    data = np.frombuffer(raw, dtype="<f4")
    return data.reshape(-1, ch).astype(np.float64), sr


def load(path):
    """Returns (samples[n, channels] in -1..1, sample_rate)."""
    try:
        with wave.open(path, "rb") as w:
            sr, ch, width, n = (
                w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
            )
            raw = w.readframes(n)
    except (wave.Error, EOFError):
        return _decode_with_ffmpeg(path)

    if width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 4:
        data = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v & 0x800000, v - 0x1000000, v)
        data = v.astype(np.float64) / 8388608.0
    elif width == 1:
        data = (np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128) / 128.0
    else:
        raise SystemExit(f"unsupported sample width {width}")
    return data.reshape(-1, ch), sr


# ---------------------------------------------------------------- segment

def find_note(mono, sr, floor_ratio=0.25):
    """Longest run whose RMS stays above a fraction of the loudest hop."""
    hop = max(1, sr // 100)
    frames = len(mono) // hop
    if frames < 3:
        return 0, len(mono)
    rms = np.sqrt(
        np.mean(mono[: frames * hop].reshape(frames, hop) ** 2, axis=1) + 1e-20
    )
    gate = rms.max() * floor_ratio
    above = rms > gate
    best = (0, 0)
    start = None
    for i, a in enumerate(above):
        if a and start is None:
            start = i
        elif not a and start is not None:
            if i - start > best[1] - best[0]:
                best = (start, i)
            start = None
    if start is not None and frames - start > best[1] - best[0]:
        best = (start, frames)
    if best[1] == best[0]:
        return 0, len(mono)
    return best[0] * hop, best[1] * hop


# ---------------------------------------------------------------- spectrum

def spectrum(x, sr):
    n = 1 << int(math.ceil(math.log2(max(len(x), 1024))))
    win = np.hanning(len(x))
    mag = np.abs(np.fft.rfft(x * win, n)) / (np.sum(win) / 2.0)
    return np.fft.rfftfreq(n, 1.0 / sr), mag


def refine_peak(freqs, mag, idx):
    """Parabolic interpolation, so f0 is not quantised to the bin grid."""
    if idx <= 0 or idx >= len(mag) - 1:
        return freqs[idx], mag[idx]
    a, b, c = mag[idx - 1], mag[idx], mag[idx + 1]
    denom = a - 2 * b + c
    d = 0.0 if denom == 0 else 0.5 * (a - c) / denom
    step = freqs[1] - freqs[0]
    return freqs[idx] + d * step, b - 0.25 * (a - c) * d


def peak_indices(mag, floor_db=-60.0):
    thresh = mag.max() * (10 ** (floor_db / 20.0))
    idx = np.where((mag[1:-1] > mag[:-2]) & (mag[1:-1] >= mag[2:]) & (mag[1:-1] > thresh))[0] + 1
    return idx


def fundamental(freqs, mag, lo=20.0, hi=2000.0):
    """Score each low peak by how much of the harmonic series it explains, so a
    missing or weak fundamental is still found and a harmonic is not mistaken
    for the note."""
    idx = peak_indices(mag)
    cands = [i for i in idx if lo <= freqs[i] <= hi]
    if not cands:
        return refine_peak(freqs, mag, int(np.argmax(mag)))[0]
    step = freqs[1] - freqs[0]
    best, best_score = None, -1.0
    for i in cands:
        f = freqs[i]
        score = 0.0
        for k in range(1, 9):
            bin_k = int(round(k * f / step))
            if bin_k < len(mag):
                score += mag[max(0, bin_k - 1):bin_k + 2].max() / k
        if score > best_score * 1.05:
            best, best_score = i, score
    return refine_peak(freqs, mag, best)[0]


def note_of(f):
    if f <= 0:
        return "-", 0.0
    midi = 69 + 12 * math.log2(f / 440.0)
    nearest = int(round(midi))
    cents = (midi - nearest) * 100.0
    return f"{NOTE_NAMES[nearest % 12]}{nearest // 12 - 1} ({nearest})", cents


# ---------------------------------------------------------------- measures

def band_energy(freqs, mag):
    power = mag ** 2
    total = power.sum() + 1e-20
    return [(lo, hi, power[(freqs >= lo) & (freqs < hi)].sum() / total) for lo, hi in BANDS]


def centroid(freqs, mag):
    p = mag ** 2
    return float((freqs * p).sum() / (p.sum() + 1e-20))


def stereo(seg, sr):
    """Width per band. side/mid is what 'enveloping' usually means; a mono
    capture reads 0.000 everywhere and a hard-panned one reads 1.000."""
    if seg.shape[1] < 2:
        return None, []
    left, right = seg[:, 0], seg[:, 1]
    corr = float(
        np.corrcoef(left, right)[0, 1] if left.std() > 0 and right.std() > 0 else 1.0
    )
    mid, side = (left + right) / 2, (left - right) / 2
    fm, mm = spectrum(mid, sr)
    _, ms = spectrum(side, sr)
    per_band = []
    for lo, hi in BANDS:
        sel = (fm >= lo) & (fm < hi)
        m = math.sqrt(float((mm[sel] ** 2).sum()) + 1e-20)
        s = math.sqrt(float((ms[sel] ** 2).sum()) + 1e-20)
        per_band.append(s / m if m > 1e-9 else 0.0)
    return corr, per_band


def envelope(mono, sr):
    """Measured over the whole note, attack included. The 90% reference is the
    peak of the first half second, not of the take: detuned voices beat, and a
    late beat swell would otherwise read as a 700 ms attack."""
    hop = max(1, sr // 200)
    frames = len(mono) // hop
    if frames < 2:
        return 0.0, 0.0, 0.0
    rms = np.sqrt(np.mean(mono[: frames * hop].reshape(frames, hop) ** 2, axis=1) + 1e-20)
    head_frames = max(1, min(frames, int(0.5 * sr / hop)))
    peak = rms[:head_frames].max()
    attack = int(np.argmax(rms >= peak * 0.9)) * hop / sr
    third = max(1, frames // 3)
    return attack, float(rms[:third].mean()), float(rms[-third:].mean())


def db(x):
    return -99.0 if x <= 1e-9 else 20 * math.log10(x)


def measure(path, window=None, n_peaks=0):
    data, sr = load(path)
    mono = data.mean(axis=1)
    if window:
        a, b = (int(v * sr) for v in window)
    else:
        a, b = find_note(mono, sr)
    # skip the attack transient, cap at 2 s of steady state
    a2 = min(a + int(0.05 * sr), b - 1)
    b2 = min(b, a2 + int(2.0 * sr))
    seg = data[a2:b2]
    seg_mono = mono[a2:b2]

    freqs, mag = spectrum(seg_mono, sr)
    f0 = fundamental(freqs, mag)
    step = freqs[1] - freqs[0]

    partials = []
    a0 = None
    for k in range(1, 9):
        bin_k = int(round(k * f0 / step))
        if bin_k >= len(mag) - 1:
            break
        local = int(np.argmax(mag[max(0, bin_k - 2):bin_k + 3])) + max(0, bin_k - 2)
        fk, ak = refine_peak(freqs, mag, local)
        if k == 1:
            a0 = ak
        partials.append((k, fk, ak / (a0 + 1e-20)))

    corr, width = stereo(seg, sr)
    attack, head, tail = envelope(mono[a:b], sr)

    # An absent odd series means f0 landed an octave low - a sub oscillator, or
    # a modulator sideband below the carrier. Say so rather than reporting the
    # wrong note.
    rel = {k: r for k, _, r in partials}
    odd = max((rel.get(k, 0.0) for k in (3, 5, 7)), default=0.0)
    octave_low = rel.get(2, 0.0) > 0.1 and odd < 0.05

    peaks = []
    if n_peaks:
        idx = peak_indices(mag, floor_db=-50.0)
        ordered = sorted(idx, key=lambda i: -mag[i])[:n_peaks]
        peaks = sorted(
            (refine_peak(freqs, mag, i)[0], mag[i] / (mag.max() + 1e-20)) for i in ordered
        )

    return {
        "path": path,
        "sr": sr,
        "channels": data.shape[1],
        "length": len(data) / sr,
        "note_at": a2 / sr,
        "note_len": (b2 - a2) / sr,
        "rms_db": db(float(np.sqrt(np.mean(seg_mono ** 2)))),
        "peak_db": db(float(np.max(np.abs(seg_mono)) if len(seg_mono) else 0.0)),
        "f0": f0,
        "note": note_of(f0),
        "octave_low": octave_low,
        "partials": partials,
        "centroid": centroid(freqs, mag),
        "bands": band_energy(freqs, mag),
        "corr": corr,
        "width": width,
        "attack": attack,
        "decay": tail / (head + 1e-20),
        "peaks": peaks,
    }


# ---------------------------------------------------------------- reporting

def band_label(lo, hi):
    def f(v):
        return f"{v // 1000}k" if v >= 1000 else str(v)
    return f"{f(lo)}-{f(hi)}"


def report(ms):
    names = [os.path.basename(m["path"]) for m in ms]
    w = max(14, max(len(n) for n in names) + 2)

    def row(label, values):
        print(f"{label:<22}" + "".join(f"{v:>{w}}" for v in values))

    row("", names)
    print("-" * (22 + w * len(ms)))
    row("length / note at", [f"{m['length']:.1f}s @{m['note_at']:.2f}s" for m in ms])
    row("analysed", [f"{m['note_len']:.2f}s" for m in ms])
    row("rms / peak dBFS", [f"{m['rms_db']:.1f} / {m['peak_db']:.1f}" for m in ms])
    row("fundamental", [f"{m['f0']:.2f} Hz" for m in ms])
    row("nearest note", [f"{m['note'][0]} {m['note'][1]:+.0f}c" for m in ms])
    row("spectral centroid", [f"{m['centroid']:.0f} Hz" for m in ms])
    row("attack to 90%", [f"{m['attack'] * 1000:.0f} ms" for m in ms])
    row("tail / head rms", [f"{m['decay']:.2f}" for m in ms])
    row("L/R correlation", [("n/a" if m["corr"] is None else f"{m['corr']:.3f}") for m in ms])

    print("\nband energy (% of total)")
    for i, (lo, hi, _) in enumerate(ms[0]["bands"]):
        row(f"  {band_label(lo, hi)} Hz", [f"{m['bands'][i][2] * 100:.1f}%" for m in ms])

    if any(m["width"] for m in ms):
        print("\nside/mid by band (0 = mono, 1 = fully decorrelated)")
        for i, (lo, hi) in enumerate(BANDS):
            row(
                f"  {band_label(lo, hi)} Hz",
                [(f"{m['width'][i]:.3f}" if m["width"] else "n/a") for m in ms],
            )

    print("\npartials, amplitude relative to the fundamental")
    for k in range(1, 9):
        vals = []
        for m in ms:
            hit = next((p for p in m["partials"] if p[0] == k), None)
            vals.append(f"{hit[2]:.3f} @{hit[1]:.0f}Hz" if hit else "-")
        row(f"  h{k}", vals)

    for m in ms:
        if m["rms_db"] < -50.0:
            print(
                f"\n! {os.path.basename(m['path'])}: near silent ({m['rms_db']:.0f} "
                f"dBFS). Everything above is noise. The usual cause is the DAW on "
                f"Realtek ASIO, which bypasses the loopback - switch it to WASAPI "
                f"or MME and take it again."
            )

    for m in ms:
        if m["octave_low"]:
            up = note_of(m["f0"] * 2)
            print(
                f"\n! {os.path.basename(m['path'])}: no odd partials above the "
                f"fundamental - f0 is probably an octave low (sub or a sideband). "
                f"The played note is more likely {m['f0'] * 2:.1f} Hz, {up[0]}."
            )

    for m in ms:
        if m["peaks"]:
            print(f"\nstrongest peaks - {os.path.basename(m['path'])}")
            for f, a in m["peaks"]:
                ratio = f / m["f0"] if m["f0"] else 0
                print(f"  {f:9.1f} Hz  {a:.3f}   x{ratio:.3f} f0")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--window", help="seconds, as start:end, instead of envelope search")
    ap.add_argument("--peaks", type=int, default=0, help="also list the N strongest peaks")
    args = ap.parse_args()

    window = None
    if args.window:
        a, b = args.window.split(":")
        window = (float(a), float(b))

    ms = []
    for path in args.files:
        if not os.path.isfile(path):
            sys.exit(f"no such file: {path}")
        ms.append(measure(path, window, args.peaks))
    report(ms)


if __name__ == "__main__":
    main()
