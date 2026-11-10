"""Make the spoken warning clip for the speaker (Day 7).

Uses Windows' built-in speech engine (System.Speech, through PowerShell) to write a
16 kHz / 16-bit / mono WAV, then strips the WAV header so only raw samples are left:
  components/alerts/assets/warn.pcm   (embedded in the firmware with EMBED_FILES)

  uv run make_audio.py
  uv run make_audio.py --text "Warning. Person detected." --rate 0
"""

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "components" / "alerts" / "assets" / "warn.pcm"
DEFAULT_TEXT = "Warning. Person detected. You are being recorded."

PS_SCRIPT = r"""
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$s.Rate = {rate}
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
$s.SetOutputToWaveFile('{wav}', $fmt)
$s.Speak('{text}')
$s.Dispose()
"""


def wav_data(path: Path) -> bytes:
    """Return the samples from a PCM WAV file (walks the RIFF chunks instead of assuming 44 bytes)."""
    raw = path.read_bytes()
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE", "not a WAV file"
    pos = 12
    fmt = None
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            assert fmt and fmt[0] == 1 and fmt[1] == 1 and fmt[2] == 16000 and fmt[5] == 16, f"unexpected format {fmt}"
            return body
        pos += 8 + size + (size & 1)
    raise ValueError("no data chunk")


def trim_silence(pcm: bytes, threshold=300) -> bytes:
    samples = struct.unpack(f"<{len(pcm) // 2}h", pcm)
    loud = [i for i, s in enumerate(samples) if abs(s) > threshold]
    if not loud:
        return pcm
    start = max(0, loud[0] - 800)              # keep 50 ms before the first sound
    end = min(len(samples), loud[-1] + 1600)   # and 100 ms after the last
    return pcm[start * 2:end * 2]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--text", default=DEFAULT_TEXT)
    ap.add_argument("--rate", type=int, default=2, help="speech speed, -10..10")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "warn.wav"
        script = PS_SCRIPT.format(rate=args.rate, wav=str(wav), text=args.text.replace("'", "''"))
        subprocess.run(["powershell", "-NoProfile", "-Command", script], check=True)
        pcm = trim_silence(wav_data(wav))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(pcm)
    print(f"{OUT}: {len(pcm)} bytes, {len(pcm) / 32000:.2f} s at 16 kHz mono")


if __name__ == "__main__":
    main()
