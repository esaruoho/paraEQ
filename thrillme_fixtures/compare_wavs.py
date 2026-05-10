#!/usr/bin/env python3
"""
Rough comparison of two float32 stereo WAVs (same length, same rate).
Usage: python3 compare_wavs.py a.wav b.wav

Requires: numpy
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np


def read_float32_stereo_wav(path: Path) -> tuple[np.ndarray, int]:
    with open(path, "rb") as f:
        hdr = f.read(12)
        if hdr[:4] != b"RIFF" or hdr[8:12] != b"WAVE":
            raise ValueError("Not RIFF WAVE")
        fmt = None
        data = None
        while True:
            chunk = f.read(8)
            if len(chunk) < 8:
                break
            cid, size = struct.unpack("<4sI", chunk)
            blob = f.read(size)
            if len(blob) < size:
                break
            if cid == b"fmt ":
                fmt = blob
            elif cid == b"data":
                data = blob
        if fmt is None or data is None:
            raise ValueError("Missing fmt or data chunk")
    audio_format, ch, rate, _, _, bits = struct.unpack_from("<HHIIHH", fmt, 0)
    if audio_format != 3 or bits != 32 or ch != 2:
        raise ValueError(f"Expected float32 stereo; got format={audio_format} ch={ch} bits={bits}")
    x = np.frombuffer(data, dtype=np.float32).reshape(-1, 2)
    return x, int(rate)


def main() -> None:
    if len(sys.argv) != 3:
        print("Usage: compare_wavs.py reference.wav candidate.wav")
        sys.exit(1)
    a, ra = read_float32_stereo_wav(Path(sys.argv[1]))
    b, rb = read_float32_stereo_wav(Path(sys.argv[2]))
    if ra != rb:
        print("Sample rate mismatch:", ra, rb)
    if a.shape != b.shape:
        print("Shape mismatch:", a.shape, b.shape, "(trim to min length for diff)")
        n = min(len(a), len(b))
        a, b = a[:n], b[:n]
    d = a - b
    print("frames:", len(a), "rate:", ra)
    print("RMS ref L/R:", float(np.sqrt(np.mean(a[:, 0] ** 2))), float(np.sqrt(np.mean(a[:, 1] ** 2))))
    print("RMS cand L/R:", float(np.sqrt(np.mean(b[:, 0] ** 2))), float(np.sqrt(np.mean(b[:, 1] ** 2))))
    print("RMS diff L/R:", float(np.sqrt(np.mean(d[:, 0] ** 2))), float(np.sqrt(np.mean(d[:, 1] ** 2))))
    print("peak diff L/R:", float(np.max(np.abs(d[:, 0]))), float(np.max(np.abs(d[:, 1]))))
    # crude band split correlation on L
    def lowpass(x, alpha: float) -> np.ndarray:
        y = np.zeros_like(x)
        y[0] = x[0]
        for i in range(1, len(x)):
            y[i] = y[i - 1] + alpha * (x[i] - y[i - 1])
        return y

    alpha = 0.01  # loose
    aL = a[:, 0].astype(np.float64)
    bL = b[:, 0].astype(np.float64)
    la, lb = lowpass(aL, alpha), lowpass(bL, alpha)
    ha, hb = aL - la, bL - lb
    corr_lo = np.corrcoef(la, lb)[0, 1]
    corr_hi = np.corrcoef(ha, hb)[0, 1]
    print("corr L low~:", float(corr_lo), "high~:", float(corr_hi))


if __name__ == "__main__":
    main()
