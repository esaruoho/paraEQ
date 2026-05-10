# ThrillMe black-box fixtures

Use these **input** wave files in your **32-bit XP** host through the **original ThrillMe**, export **float32 WAV** (or the best your chain allows), then we can compare against ParaEQ’s `ThrillMeTone` path or tune coefficients.

## What the marketing doc does *not* pin down

Crossover corner vs −3 dB point, Q of combined stages, exact envelope attack/release vs level, threshold curve in linear vs log domain, limiter topology (single vs multistage), stereo linking, oversampling factor, denorm policy, and the full **parameter → internal** map. Fixtures fill that gap.

## Requirements (on the machine that generates inputs / analyzes)

```bash
pip install numpy
```

(Optional, for nicer plots later: `pip install matplotlib soundfile`.)

## 1. Generate inputs (any OS)

From repo root:

```bash
python3 thrillme_fixtures/generate_inputs.py
```

Outputs go to `thrillme_fixtures/inputs/` (mono + stereo, various lengths).

## 2. Capture through original ThrillMe (your XP rig)

For each input file:

1. Fixed sample rate (e.g. **44100** or **48000** — use one rate per batch; note it in the filename).
2. Same buffer size if the host exposes it (optional but good for repeatability).
3. **Bypass all other plugins.** ThrillMe only (or documented chain).
4. Document **every** ThrillMe control: spectral %, threshold, ratio, and any hidden/internal pages if the UI has more.
5. Render **offline** to WAV if possible (less DAW tail / latency variance).

Name captures like:

`captures/original_44100_thr0_spec0_r128_sine1k.wav`

Use a short tag in the filename: `thr` = threshold dB, `spec` = spectral 0–100, `r` = displayed ratio `:1` (as shown on the original meter if applicable).

## 3. Same inputs through ParaEQ301

In your macOS host, insert ParaEQ301, same sample rate, solo the ThrillMe path if needed, match **as closely as you can** the mapped parameters, render the **same** input file to `captures/paraeq301_...`.

## 4. Quick numeric compare

```bash
python3 thrillme_fixtures/compare_wavs.py captures/original_...wav captures/paraeq301_...wav
```

Prints RMS level, peak, rough spectral delta (FFT bin RMS difference). Tight alignment needs per-fixture tolerances and maybe time-alignment (latency).

## Fixture list (what each input is for)

| File | Purpose |
|------|---------|
| `impulse_mono.wav` | Reconstruction / IR-like view of linear stages + smearing |
| `log_sweep_10s.wav` | Band splits and spectral tilt vs frequency |
| `white_noise_5s.wav` | Stationary spectrum, GR noise modulation |
| `sine_1k_-12dbfs_5s.wav` | Steady-state harmonic structure, limiter |
| `multitone_3s.wav` | Intermod + multiband GR interaction |
| `step_envelope_1k.wav` | Attack/release / envelope law |

Stereo files duplicate L=R for level; `impulse_stereo_lr.wav` has L impulse only for stereo path checks.

## Legal note

Capturing **your own** audio through software you are **licensed** to use for personal R&D is normal. **Redistributing** the original plugin’s binary or derived decompiled code is a different matter. This folder is only **test signals** and **compare scripts** — no ThrillMe binary included.
