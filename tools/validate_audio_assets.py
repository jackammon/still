#!/usr/bin/env python3
"""Check immutable source PCM, loop endpoints, and BODY/SWIRL reconstruction."""
import sys
import wave
from pathlib import Path

def read_pcm24(path):
    with wave.open(str(path), "rb") as wav:
        channels, rate, width, frames = wav.getnchannels(), wav.getframerate(), wav.getsampwidth(), wav.getnframes()
        raw = wav.readframes(frames)
    if width != 3:
        raise AssertionError(f"{path.name}: expected 24-bit PCM")
    values = []
    for offset in range(0, len(raw), 3):
        value = raw[offset] | (raw[offset+1] << 8) | (raw[offset+2] << 16)
        if value & 0x800000: value -= 1 << 24
        values.append(value)
    return channels, rate, frames, [values[i:i+channels] for i in range(0, len(values), channels)]

def check(ok, message):
    if not ok: raise AssertionError(message)

def main():
    root = Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]/"Assets"
    stage = root/"Stage"
    files = {"body":"D_A_BODY_STAGE_48k.wav","swirl":"D_A_SWIRL_STAGE_48k.wav",
             "full":"D_A_FULL_STAGE_BACKUP_48k.wav","air":"D_C_AIR_STAGE_48k.wav"}
    decoded = {}
    for role,name in files.items():
        channels,rate,frames,data=read_pcm24(stage/name)
        check(channels==2 and rate==48000 and frames>0, f"{name}: expected stereo 48 kHz")
        check(data[0]==data[-1], f"{name}: first/last frames differ")
        decoded[role]=(frames,data)
        print(f"{role}: stereo 48 kHz, {frames} frames, exact endpoint match")
    n,body=decoded["body"]; ns,swirl=decoded["swirl"]; nf,full=decoded["full"]
    check(n==ns==nf, "BODY, SWIRL, and FULL lengths differ")
    error=max(abs(body[i][c]+swirl[i][c]-full[i][c]) for i in range(n) for c in range(2))
    check(error<=2, f"BODY + SWIRL error is {error} PCM counts")
    print(f"BODY + SWIRL reconstruction: max error {error} PCM counts")
    for name in ("REFERENCE_stage_morph_A_only.mp3","REFERENCE_stage_morph_A_plus_C.mp3"):
        check((root/"References"/name).is_file(), f"missing listening reference {name}")
    print("Listening MP3 references found; not used as loop sources.")

if __name__=="__main__": main()
