#!/usr/bin/env python3
import sys
import os
import json
import subprocess
from vosk import Model, KaldiRecognizer, SetLogLevel

SetLogLevel(-1)

def main():
    if len(sys.argv) < 2:
        sys.exit(1)
    audio_path = sys.argv[1]
    if not os.path.exists(audio_path):
        sys.exit(1)

    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    model_dir = os.path.join(base_dir, "models", "vosk-model-small-fr-0.22")
    if not os.path.exists(model_dir):
        sys.stderr.write("Modèle vocal français introuvable.\n")
        sys.exit(1)

    model = Model(model_dir)
    rec = KaldiRecognizer(model, 16000)

    proc = subprocess.Popen(
        [
            "ffmpeg", "-nostdin", "-loglevel", "quiet",
            "-i", audio_path,
            "-ar", "16000", "-ac", "1", "-f", "s16le", "-"
        ],
        stdout=subprocess.PIPE
    )

    parts = []
    while True:
        data = proc.stdout.read(4000)
        if len(data) == 0:
            break
        if rec.AcceptWaveform(data):
            res = json.loads(rec.Result())
            t = res.get("text", "").strip()
            if t:
                parts.append(t)

    final_res = json.loads(rec.FinalResult())
    ft = final_res.get("text", "").strip()
    if ft:
        parts.append(ft)

    full_text = " ".join(parts).strip()
    if full_text:
        full_text = full_text[0].upper() + full_text[1:]
    sys.stdout.write(full_text)

if __name__ == "__main__":
    main()
