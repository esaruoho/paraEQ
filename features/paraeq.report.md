# Report Card — ParaEQ 301

**Unit:** JUCE parametric-EQ + saturation / waveshaping / resonator plugin (AU · VST3 · Standalone)
**Skin:** DSP / signal chain (claim = input signal → stage → measurable output)
**Status of this card:** retrofit study — ParaEQ shipped *without* a card. Grades describe what exists **today**.
**Source card:** [`features/paraeq.feature`](paraeq.feature) · **Session:** _MISSING_

---

## What this card spawns

- **Codespace** — `Source/PluginProcessor.{h,cpp}` (chain + params + 15 programs) · `Source/PluginEditor.{h,cpp}` (7 tabs + EQ-curve/spectrum paint) · DSP blocks: `ParametricEqDesign.h`, `VaNonlinearSvf.h`, `ThrillMeTone.h`, `PakettiShapers.h` + `ChebyLutBuilder`, `AutoparametricResonator.h`, `ParametricExcitation.h`.
- **Thinkspace** — `features/paraeq.session.md` — **missing**.
- **Areaspace** — OWNS the in-plugin DSP chain, parameters, UI, programs. MUST NOT own the host, the DAW render, or JUCE. **Verification (oracle comparison) is owed but unbuilt.**

## Grade legend

| Tag | Meaning |
|---|---|
| `@built-runs` | compiles to AU/VST3/Standalone and runs without crash (artifacts 2026-05-15; `./ui.sh` launches) |
| `@in-chain` | wired into `processBlock` at a known, cited position |
| `@ref-designed` | DSP follows a **named** reference design (cited) — *not verified against it* |
| `@untested` | no automated DSP verification: no oracle run, no assertion, no capture |
| `@fixtures-only` | deterministic test inputs exist but no harness/captures wired |
| `@caveat` | known-incomplete or sharp edge, stated honestly |

## Signal-chain order (what a sample traverses)

`punch → pre-emph shelf → ThrillMe1 → Shaper(Magnet|Cheby) → core sat → low shelf → [low-chain tap] → Mid1 → [mid-chain tap] → Mid2 → high shelf → VA-SVF → anharm bank → ThrillMe2 → post shelf → lo-fi → glue → ring → pink-bal → APR ∥ → ParEx ∥ → output trim → limiter → dry/wet`
*(cite: `PluginProcessor.cpp:1460 processRoastAndEqBlock`, entry `:2012 processBlock`)*

## RESULT (what shipped)

- **Feature delivery:** long history; latest `b088520` + siblings — direct-push to `esaruoho` main, no PR.
- **Build artifacts:** `build/ParaEQ301_artefacts/Release/{AU,VST3,Standalone}` (2026-05-15).
- **This card authored:** _pending commit_
- **Triad status:** `.feature` = partial · `.session` = **missing** · RESULT = present.

---

## Scenarios (the Gherkin report)

### ✅ `@built-runs` — The plugin builds and loads in a host
> **Given** a macOS host (AU/VST3) or the Standalone app
> **When** ParaEQ 301 is loaded and audio runs through it
> **Then** it instantiates and processes without crashing

- **cite:** `deploy.sh` → `build/ParaEQ301_artefacts/Release/{AU,VST3,Standalone}`; `./ui.sh` launches
- *"doesn't crash" is **not** "DSP is correct" — see the verification gap.*

### ⚠️ `@in-chain @ref-designed @untested` — 4-band parametric EQ (Orfanidis biquads)
> **Given** a band's centre/bandwidth/gain are set
> **When** a signal passes low-shelf → Mid1 → Mid2 → high-shelf
> **Then** each band boosts/cuts per the Orfanidis peaking design

- **cite:** `ParametricEqDesign.h:1 makeOrfanidisPeakCoefficients` (sinh-mapped digital BW, peq.m)
- **cite:** `PluginProcessor.cpp:1909/1917/1923/1924` (low/Mid1/Mid2/high)
- *ref-designed against Orfanidis peq.m; **no magnitude-response test** run against it.*

### ⚠️ `@built @untested` — "Linear EQ only" reference path
> **Given** `linearEqListen` is enabled
> **When** audio is processed
> **Then** only the linear IIR EQ (+ APR/ParEx) runs — saturation/roast/shapers bypassed

- **cite:** `PluginProcessor.cpp:546` param, `:1787` the `if(linearListen)` branch
- *intended A/B reference; bypass-correctness not asserted.*

### 🔶 `@in-chain @fixtures-only @caveat` — ThrillMe tone (spectral emphasis + multiband dynamics)
> **Given** ThrillMe 1/2 engaged with spectral/threshold/ratio set
> **When** a signal passes through
> **Then** it gets shelf/peak emphasis, 3-band compression, and a tanh limit

- **cite:** `ThrillMeTone.h:99 processChannel` (4 RBJ filters + 3-band comp + tanh limiter)
- **cite:** `PluginProcessor.cpp:1879 thrillMe1` (pre-EQ), `:1948 thrillMe2` (post-EQ)
- **@fixtures-only:** `thrillme_fixtures/` has 7 deterministic inputs but **no reference captures** and **no comparison run**.
- **@caveat:** the reverse-engineering is **explicitly incomplete** (README: crossover corner / Q / threshold curve / limiter topology not pinned down).

### ⚠️ `@in-chain @ref-designed @untested` — VA nonlinear SVF (saturating bandpass resonance)
> **Given** `svfEnable` on with cf/Q/drive set
> **When** a signal passes the resonator (parallel, post high-shelf)
> **Then** a tanh-limited bandpass resonance is mixed in

- **cite:** `VaNonlinearSvf.h:20 processBandpassNonlinear` (trapezoidal ZDF, tanh in loop)
- **cite:** `PluginProcessor.cpp:1927`
- *an analytic oracle for the linear case **exists** (`PluginProcessor.cpp:71-93 svfLinearBandpassTransferHz`) but is **not run** as a regression check — the single most "test-ready" block, untested.*

### ⚠️ `@in-chain @untested` — Roast core saturation / crunch
> **Given** `coreDirt`/`coreCrunch`/`roastCoreShape` set
> **When** audio passes the core stages (incl. inter-stage low/mid taps)
> **Then** it is saturated per the chosen shape (Classic/Warm/Aggro/Tape/Punch)

- **cite:** `PluginProcessor.cpp:1906 applyCoreSaturation`; shape choices `459-466`; taps `:1915`, `:1921`
- *no THD / distortion-spectrum verification.*

### ⚠️ `@in-chain @untested` — Paketti Shaper (Magnet | Chebyshev LUT)
> **Given** `shaperMode` = Magnet or Chebyshev with controls set
> **When** a signal passes the shaper (pre-EQ)
> **Then** it is reshaped by the asymmetric Magnet saturator or the Chebyshev harmonic LUT

- **cite:** `PluginProcessor.cpp:1882-1902` (Magnet 1890, Chebyshev 1896)
- **cite:** `PakettiShapers.h:220 magnetProcessSample`, `:80 rebuildChebyLut` (4096-pt); `ChebyLutBuilder` background double-buffer + sync fallback
- *LUT glitch-freeness and harmonic accuracy unverified (ear only).*

### ⚠️ `@in-chain @ref-designed @untested` — Anharmonic partial bank (stiff-string)
> **Given** `anharmBankEnable` with fundamental / inharmonicity B / partials set
> **When** a signal passes the partial bank
> **Then** resonant peaks sit at inharmonic (stretched) partial frequencies

- **cite:** `ParametricEqDesign.h:62 stiffStringPartialHz` `f_n = n·f0·√(1+B(n²−1))`
- **cite:** `PluginProcessor.cpp:1936`
- *formula standard; partial placement not measured.*

### ⚠️ `@in-chain @untested` — Auto-Parametric Resonator (APR)
> **Given** `aprEnable` with baseHz/Q/pump/autoTrack/drive set
> **When** a signal passes APR in parallel
> **Then** a bandpass whose centre is modulated by envelope + pump is mixed in

- **cite:** `AutoparametricResonator.h:33 process`; `PluginProcessor.cpp:1961`
- *stability across the parameter range unverified.*

### ⚠️ `@in-chain @ref-designed @untested` — Parametric Excitation (ParEx, Mathieu)
> **Given** `parexEnable` with baseHz/Q/ratio/depth/drive + pump source set
> **When** a signal passes ParEx in parallel
> **Then** parametric pumping injects energy near the Mathieu instability tongue

- **cite:** `ParametricExcitation.h:44 process`; `PluginProcessor.cpp:1967`
- *ref-designed on the Mathieu equation (depth clamped ~1.6/Q); injection not measured.*

### ⚠️ `@built @untested` — Motion / LFO modulates EQ bands
> **Given** Motion engaged on a band (rate/depth or host-sync division)
> **When** the transport runs
> **Then** the band's gain/cf/bw is modulated by the LFO

- **cite:** `createParameterLayout` lfo* params `819-863`
- *no test that depth/sync matches the set values.*

### ⚠️ `@in-chain @untested` — Output limiter + master dry/wet
> **Given** `outLimOn` with threshold/release and a dry/wet setting
> **When** the processed signal reaches the output
> **Then** it is limited (if on) and crossfaded with dry per `masterDryWet`

- **cite:** `PluginProcessor.cpp:2104-2116` limiter, `:2129-2141` dry/wet crossfade

### ⚠️ `@built @untested` — 15 factory programs
> **Given** the user selects a program (Init … APR foley)
> **When** it loads
> **Then** the full parameter set for that program is applied

- **cite:** `PluginProcessor.cpp:2772 getProgramName`; `kNumFactoryPrograms=15` (`PluginProcessor.h:36`)
- **@caveat:** host program-index sync after session recall is a known TODO.

### ⚠️ `@built @untested` — Per-block one-pole smoothing (anti-zipper)
> **Given** a band/shaper parameter is moved during playback
> **When** the next block is processed
> **Then** the parameter eases to target over ~20 ms (no click)

- **cite:** `PluginProcessor.cpp:946-948` (~20 ms block coeff), `:1622-1631` per-band step

---

## The verification gap (the honest spine)

`thrillme_fixtures/` has 7 deterministic input WAVs + `compare_wavs.py` — **but**:

- **No reference captures** from the original ThrillMe (`captures/` empty).
- `compare_wavs.py` prints RMS / peak / correlation with **no tolerance, no pass/fail**.
- **No renderer** pipes inputs through the plugin; **no CI**; **no assertions** in the audio path.

**⇒ Every DSP scenario above is `@untested` in the strict sense:** it is *built* and *runs*, the design often follows a *cited reference*, but **nothing measures the output**. ParaEQ is ship-ready *by ear*, scientifically *unverified*.

## Where ParaEQ stands against the four properties

| Property | Status | Note |
|---|---|---|
| **P1** verifiable claims | ✓ | this card |
| **P2** linked to innards | ✓ | every scenario cites file:line proc |
| **P3** honestly graded | ✓ | `@untested` is the dominant grade — on purpose |
| **P4** two-way back-link | ✗ | `Source/*.{h,cpp}` carry **no** `// FEATURE-CARD >>` marker yet |
| **Triad** | partial | `.feature` partial · `.session` **missing** · RESULT present |

**⇒ ParaEQ is, by the report-card definition, an INCOMPLETE unit:** real and shipping, but its claims are unmeasured and the back-links + session are still owed. The highest-leverage next step is also the most honest one — wire `thrillme_fixtures/compare_wavs.py` (and the existing SVF oracle) into an actual render+assert harness so the `@untested` grades can earn `@sim-verified`.
