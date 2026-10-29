#!/usr/bin/env python3
import sys
import os
import json
import signal
import subprocess
from vosk import Model, KaldiRecognizer, SetLogLevel

SetLogLevel(-1)

def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    model_dir = os.path.join(base_dir, "models", "vosk-model-small-fr-0.22")
    if not os.path.exists(model_dir):
        print("ERROR:Modèle vocal français introuvable.", flush=True)
        sys.exit(1)

    model = Model(model_dir)
    rec = KaldiRecognizer(model, 16000)

    def _set_pdeathsig():
        try:
            import ctypes
            libc = ctypes.CDLL("libc.so.6")
            libc.prctl(1, signal.SIGKILL)  # PR_SET_PDEATHSIG = 1
        except Exception:
            pass

    proc = subprocess.Popen(
        [
            "ffmpeg", "-nostdin", "-loglevel", "quiet",
            "-f", "pulse", "-i", "default",
            "-ar", "16000", "-ac", "1", "-f", "s16le", "-"
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        preexec_fn=_set_pdeathsig
    )

    running = True
    def handle_stop(signum, frame):
        nonlocal running
        running = False
        try:
            proc.kill()
        except Exception:
            pass

    signal.signal(signal.SIGTERM, handle_stop)
    signal.signal(signal.SIGINT, handle_stop)

    print("READY", flush=True)

    try:
        while running:
            data = proc.stdout.read(1600)
            if not data:
                break
            if rec.AcceptWaveform(data):
                res = json.loads(rec.Result())
                txt = res.get("text", "").strip()
                if txt:
                    print(f"FINAL:{txt}", flush=True)
            else:
                pres = json.loads(rec.PartialResult())
                ptxt = pres.get("partial", "").strip()
                if ptxt:
                    print(f"PARTIAL:{ptxt}", flush=True)
    except Exception:
        pass

    try:
        fres = json.loads(rec.FinalResult())
        ftxt = fres.get("text", "").strip()
        if ftxt:
            print(f"FINAL:{ftxt}", flush=True)
    except Exception:
        pass

    try:
        proc.kill()
    except Exception:
        pass

if __name__ == "__main__":
    main()
