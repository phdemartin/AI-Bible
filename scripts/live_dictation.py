#!/usr/bin/env python3
import sys
import os
import json
import signal
import subprocess
from vosk import Model, KaldiRecognizer, SetLogLevel

SetLogLevel(-1)

def find_model_dir():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    home_dir = os.path.expanduser("~")
    candidates = [
        os.path.join(base_dir, "models", "vosk-model-small-fr-0.22"),
        os.path.join(home_dir, ".local", "share", "ai-bible", "models", "vosk-model-small-fr-0.22"),
        "/usr/share/ai-bible/models/vosk-model-small-fr-0.22",
        os.path.join(home_dir, ".gemini", "antigravity", "scratch", "Bible_Tool", "models", "vosk-model-small-fr-0.22"),
    ]
    for cand in candidates:
        if os.path.exists(cand):
            return cand

    # Téléchargement automatique au premier usage dans ~/.local/share/ai-bible/models/
    try:
        import urllib.request
        import zipfile
        target_parent = os.path.join(home_dir, ".local", "share", "ai-bible", "models")
        os.makedirs(target_parent, exist_ok=True)
        zip_path = os.path.join(target_parent, "vosk-model-small-fr-0.22.zip")
        url = "https://alphacephei.com/vosk/models/vosk-model-small-fr-0.22.zip"
        urllib.request.urlretrieve(url, zip_path)
        with zipfile.ZipFile(zip_path, "r") as zf:
            zf.extractall(target_parent)
        os.remove(zip_path)
        extracted = os.path.join(target_parent, "vosk-model-small-fr-0.22")
        if os.path.exists(extracted):
            return extracted
    except Exception:
        pass
    return None

def main():
    model_dir = find_model_dir()
    if not model_dir:
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
