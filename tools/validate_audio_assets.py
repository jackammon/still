#!/usr/bin/env python3
"""Check immutable source PCM, loop endpoints, and BODY/SWIRL reconstruction."""
import hashlib
import json
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

def read_flac_streaminfo(path):
    with path.open("rb") as source:
        check(source.read(4)==b"fLaC", f"{path.name}: missing FLAC signature")
        header=source.read(4)
        check(len(header)==4 and (header[0]&0x7f)==0, f"{path.name}: missing STREAMINFO")
        size=int.from_bytes(header[1:],"big")
        check(size==34, f"{path.name}: invalid STREAMINFO size")
        info=source.read(size)
    packed=int.from_bytes(info[10:18],"big")
    return {"sample_rate":packed>>44, "channels":((packed>>41)&7)+1,
            "bits_per_sample":((packed>>36)&31)+1, "frames":packed&((1<<36)-1)}

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

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

    manifest_path=root/"Banks"/"BANK_MANIFEST.json"
    manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
    check(manifest.get("reference_key")=="D" and manifest.get("reference_samples_are_unprocessed") is True,
          "bank manifest must identify the untouched D reference bank")
    rendered={(item["key"],item["stem"]):item for item in manifest.get("derived_samples",[])}
    expected_keys=("C","Cs","Ds","E","F","Fs","G","Gs","A","As","B")
    for key in expected_keys:
        for stem,frames in (("BODY",276000),("SWIRL",276000),("AIR",249600)):
            item=rendered.get((key,stem))
            check(item is not None, f"pitch bank is missing {key} {stem}")
            path=root.parent/item["path"]
            metadata=read_flac_streaminfo(path)
            check(metadata=={"sample_rate":48000,"channels":2,"bits_per_sample":24,"frames":frames},
                  f"{path.name}: pitch bank must remain stereo 48 kHz 24-bit with {frames} frames")
            check(sha256(path)==item["sha256"], f"{path.name}: SHA-256 differs from the bank manifest")
        check(rendered[(key,"BODY")]["frames"]==rendered[(key,"SWIRL")]["frames"],
              f"{key}: BODY and SWIRL are not sample-aligned")
    print("Alternate-key FLAC banks: all 11 keys, stereo 48 kHz 24-bit, equal BODY/SWIRL frames and verified SHA-256.")

if __name__=="__main__": main()
