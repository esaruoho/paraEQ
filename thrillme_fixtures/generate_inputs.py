#!/usr/bin/env python3
"""
Generate deterministic WAV inputs for ThrillMe black-box capture.
Requires: numpy. Outputs IEEE float32 PCM WAV (format 3).

Usage:
  python3 generate_inputs.py
"""

from __future__ import annotations

import math
import struct
from pathlib import Path

import numpy as np


def write_wav_float32_stereo(path: Path, interleaved_lr: np.ndarray, sample_rate: int) -> None:
    """interleaved_lr shape (N*2,) float32 L,R,L,R,..."""
    assert interleaved_lr.dtype == np.float32
    path.parent.mkdir(parents=True, exist_ok=True)
    _write_riff_float32_stereo(path, interleaved_lr, sample_rate)


def _write_riff_float32_stereo(path: Path, interleaved_lr: np.ndarray, sample_rate: int) -> None:
    byte_rate = sample_rate * 2 * 4
    block_align = 8
    data_bytes = interleaved_lr.nbytes
    # fmt chunk for IEEE float stereo
    fmt_chunk = struct.pack(
        "<HHIIHH",
        3,
        2,
        sample_rate,
        byte_rate,
        block_align,
        32,
    )
    riff_size = 4 + (8 + 16) + (8 + data_bytes)
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", riff_size))
        f.write(b"WAVEfmt ")
        f.write(struct.pack("<I", 16))
        f.write(fmt_chunk)
        f.write(b"data")
        f.write(struct.pack("<I", data_bytes))
        f.write(interleaved_lr.tobytes())


def stereo_from_mono(m: np.ndarray) -> np.ndarray:
    m = m.astype(np.float32)
    return np.stack([m, m], axis=1).reshape(-1)


def impulse(sr: int, n: int = 48000) -> np.ndarray:
    x = np.zeros(n, dtype=np.float32)
    x[1000] = 1.0
    return stereo_from_mono(x)


def impulse_stereo_lr(sr: int, n: int = 48000) -> np.ndarray:
    L = np.zeros(n, dtype=np.float32)
    R = np.zeros(n, dtype=np.float32)
    L[1000] = 1.0
    R[5000] = 1.0
    return np.stack([L, R], axis=1).reshape(-1)


def log_sweep(sr: int, dur_s: float = 10.0) -> np.ndarray:
    n = int(sr * dur_s)
    t = np.arange(n, dtype=np.float64) / sr
    f0, f1 = 20.0, 20000.0
    phase = 2.0 * np.pi * f0 * dur_s / math.log(f1 / f0) * (np.exp(t / dur_s * math.log(f1 / f0)) - 1.0)
    x = 0.2 * np.sin(phase).astype(np.float32)
    return stereo_from_mono(x)


def white_noise(sr: int, dur_s: float = 5.0, seed: int = 42) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = int(sr * dur_s)
    x = (0.05 * rng.standard_normal(n)).astype(np.float32)
    return stereo_from_mono(x)


def sine_tone(sr: int, hz: float, dur_s: float = 5.0, dbfs: float = -12.0) -> np.ndarray:
    n = int(sr * dur_s)
    t = np.arange(n, dtype=np.float64) / sr
    lin = 10.0 ** (dbfs / 20.0)
    x = (lin * np.sin(2.0 * np.pi * hz * t)).astype(np.float32)
    return stereo_from_mono(x)


def multitone(sr: int, dur_s: float = 3.0) -> np.ndarray:
    n = int(sr * dur_s)
    t = np.arange(n, dtype=np.float64) / sr
    freqs = [440.0, 554.37, 880.0]
    x = np.zeros(n, dtype=np.float64)
    for f in freqs:
        x += np.sin(2.0 * np.pi * f * t)
    x = (0.1 * x / len(freqs)).astype(np.float32)
    return stereo_from_mono(x)


def step_envelope_1k(sr: int, dur_s: float = 4.0) -> np.ndarray:
    """1 kHz carrier; amplitude steps 0.1 -> 0.4 at 1s, down at 2s for envelope test."""
    n = int(sr * dur_s)
    t = np.arange(n, dtype=np.float64) / sr
    env = np.zeros(n, dtype=np.float64)
    env[:] = 0.1
    env[int(1.0 * sr) :] = 0.4
    env[int(2.0 * sr) :] = 0.1
    carrier = np.sin(2.0 * np.pi * 1000.0 * t)
    x = (env * carrier).astype(np.float32)
    return stereo_from_mono(x)


def main() -> None:
    root = Path(__file__).resolve().parent
    out = root / "inputs"
    out.mkdir(parents=True, exist_ok=True)
    sr = 48000

    files = [
        ("impulse_mono_like.wav", impulse(sr)),
        ("impulse_stereo_lr.wav", impulse_stereo_lr(sr)),
        ("log_sweep_20hz_20khz_10s.wav", log_sweep(sr, 10.0)),
        ("white_noise_5s.wav", white_noise(sr, 5.0)),
        ("sine_1k_-12dbfs_5s.wav", sine_tone(sr, 1000.0, 5.0, -12.0)),
        ("multitone_440_554_880_3s.wav", multitone(sr, 3.0)),
        ("step_envelope_1k_4s.wav", step_envelope_1k(sr, 4.0)),
    ]

    for name, data in files:
        p = out / name
        _write_riff_float32_stereo(p, data.astype(np.float32), sr)
        print("wrote", p, "frames", data.size // 2)

    print("\nSample rate used:", sr, "Hz (re-generate with edited sr=44100 if needed).")


if __name__ == "__main__":
    main()
