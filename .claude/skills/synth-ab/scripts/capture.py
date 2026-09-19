"""Record the Windows loopback to a WAV, with a Stop button so the take is never
cut off mid-note.

  python capture.py --out take.wav              # Tk window, press Stop when done
  python capture.py --out take.wav --seconds 8  # headless, fixed length
  python capture.py --list                      # show dshow audio devices

Stopping writes 'q' to ffmpeg's stdin rather than killing it: a killed ffmpeg
leaves a WAV header the `wave` module refuses to open.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

DEVICE = "Stereo Mix (Realtek(R) Audio)"
FFMPEG_FALLBACK = os.path.expandvars(
    r"%USERPROFILE%\Downloads\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe"
)


def find_ffmpeg():
    exe = shutil.which("ffmpeg") or (
        FFMPEG_FALLBACK if os.path.isfile(FFMPEG_FALLBACK) else None
    )
    if exe is None:
        sys.exit("ffmpeg not found on PATH or in Downloads")
    return exe


def list_devices(ffmpeg):
    out = subprocess.run(
        [ffmpeg, "-hide_banner", "-list_devices", "true", "-f", "dshow", "-i", "dummy"],
        capture_output=True,
        text=True,
    ).stderr
    for line in out.splitlines():
        if "(audio)" in line:
            print(line.split("] ", 1)[-1])


def start(ffmpeg, device, out_path, seconds=None):
    cmd = [
        ffmpeg, "-hide_banner", "-loglevel", "warning",
        "-f", "dshow", "-audio_buffer_size", "50", "-i", f"audio={device}",
        "-ac", "2", "-ar", "48000",
    ]
    if seconds:
        cmd += ["-t", str(seconds)]
    cmd += ["-y", out_path]
    return subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)


def stop(proc):
    """Ask ffmpeg to finish so it closes the WAV header properly."""
    if proc.poll() is None:
        try:
            proc.stdin.write(b"q")
            proc.stdin.flush()
        except (OSError, ValueError):
            pass
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.terminate()
        proc.wait(timeout=5)


def run_with_button(proc, out_path, label):
    import tkinter as tk

    root = tk.Tk()
    root.title("Rhino A/B capture")
    root.attributes("-topmost", True)
    root.geometry("320x150")

    tk.Label(root, text=label, font=("Segoe UI", 11)).pack(pady=(14, 2))
    clock = tk.Label(root, text="0.0 s", font=("Segoe UI", 22))
    clock.pack()

    started = time.time()
    done = {"stopped": False}

    def finish():
        if not done["stopped"]:
            done["stopped"] = True
            root.destroy()

    tk.Button(root, text="Stop", width=14, height=2, command=finish).pack(pady=8)
    root.protocol("WM_DELETE_WINDOW", finish)

    def tick():
        if proc.poll() is not None:  # ffmpeg died on its own
            finish()
            return
        clock.config(text=f"{time.time() - started:.1f} s")
        root.after(100, tick)

    tick()
    root.mainloop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="take.wav")
    ap.add_argument("--seconds", type=int, help="fixed length, no window")
    ap.add_argument("--device", default=DEVICE)
    ap.add_argument("--label", default="Play the note, then press Stop")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    ffmpeg = find_ffmpeg()
    if args.list:
        list_devices(ffmpeg)
        return

    out_path = os.path.abspath(args.out)
    proc = start(ffmpeg, args.device, out_path, args.seconds)

    if args.seconds:
        print(f"recording {args.seconds}s to {out_path}", flush=True)
        proc.wait()
    else:
        print(f"recording to {out_path} - press Stop when done", flush=True)
        run_with_button(proc, out_path, args.label)
        stop(proc)

    err = proc.stderr.read().decode(errors="replace").strip()
    if proc.returncode not in (0, 255) or not os.path.isfile(out_path):
        sys.exit(f"ffmpeg failed ({proc.returncode})\n{err}")
    if err:
        print(err, file=sys.stderr)
    print(f"wrote {out_path} ({os.path.getsize(out_path) / 1e6:.2f} MB)")


if __name__ == "__main__":
    main()
