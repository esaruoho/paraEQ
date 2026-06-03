as # =============================================================================
# REPORT CARD: ParaEQ 301 — JUCE parametric-EQ + saturation/resonator plugin
# Skin: DSP / signal chain (claim = input signal → stage → measurable output)
# Convention: ~/.claude/skills/report-card/SKILL.md
# SESSION >> features/paraeq.session.md  (the spawning conversation — TODO, gap)
# RENDER  >> features/paraeq.report.md   (human-readable Markdown view)
#
# STATUS: retrofit study. ParaEQ shipped without a card. Grades describe TODAY.
#
# ── WHAT THIS CARD SPAWNS ───────────────────────────────────────────────────
#   Codespace : Source/PluginProcessor.{h,cpp} (chain + params + programs),
#               Source/PluginEditor.{h,cpp} (7 tabs + EQ-curve/spectrum paint),
#               DSP blocks: ParametricEqDesign.h (Orfanidis biquad + stiff-string),
#               VaNonlinearSvf.h, ThrillMeTone.h, PakettiShapers.h + ChebyLutBuilder,
#               AutoparametricResonator.h, ParametricExcitation.h.
#   Thinkspace: features/paraeq.session.md — MISSING.
#   Areaspace : OWNS the in-plugin DSP chain, params, UI, programs. MUST NOT own
#               the host, the DAW render, or JUCE itself. Verification (oracle
#               comparison) is OWED but unbuilt.
#
# ── report-card legend (grades in use) ──────────────────────────────────────
#   @built-runs   - compiles to AU/VST3/Standalone and runs without crash
#                   (build artifacts present, 2026-05-15; ./ui.sh launches)
#   @in-chain     - wired into processBlock at a known, cited position
#   @ref-designed - DSP follows a NAMED reference design (cited) — not verified against it
#   @untested     - no automated DSP verification: no oracle run, no assertion, no capture
#   @fixtures-only- deterministic test inputs exist but no harness/captures wired
#   @caveat       - known-incomplete or sharp edge, stated honestly
#
# ── innards cited (file:line proc) ──────────────────────────────────────────
#   PluginProcessor.cpp  processBlock(2012); processRoastAndEqBlock(1460);
#     createParameterLayout(324); getProgramName(2772); band smoothing(1622-1631);
#     svfLinearBandpassTransferHz oracle(71-93)
#   ParametricEqDesign.h makeOrfanidisPeakCoefficients(1); stiffStringPartialHz(62)
#   VaNonlinearSvf.h     processBandpassNonlinear(20)
#   ThrillMeTone.h       processChannel(99); updateSpectralCoeffs(72)
#   PakettiShapers.h     rebuildChebyLut(80); magnetProcessSample(220)
#   ChebyLutBuilder.{h,cpp} resolveLut / background run()
#   AutoparametricResonator.h process(33)
#   ParametricExcitation.h   process(44)
#
# ── RESULT (third leg) ──────────────────────────────────────────────────────
#   Feature delivery : long history, latest b088520 + siblings, direct-push to esaruoho main.
#   Build artifacts  : build/ParaEQ301_artefacts/Release/{AU,VST3,Standalone} (2026-05-15).
#   This card authored: (pending commit of features/paraeq.feature + .report.md)
#   Triad status     : .feature = THIS (partial) · .session = MISSING · RESULT = here.
# =============================================================================

Feature: ParaEQ 301 — a parametric EQ with saturation, shaping and resonator cores
  As someone mixing audio,
  I want a 4-band parametric EQ plus optional saturation, waveshaping, and
  resonator stages, in a defined signal-chain order,
  So that I can shape tone and add colour from one plugin.

  @built-runs
  Scenario: The plugin builds and loads in a host
    # cite: deploy.sh → build/ParaEQ301_artefacts/Release/{AU,VST3,Standalone}
    # cite: build artifacts present 2026-05-15; ./ui.sh launches Standalone
    Given a macOS host (AU or VST3) or the Standalone app
    When ParaEQ 301 is loaded and audio runs through it
    Then it instantiates and processes without crashing
    # @built-runs only — "doesn't crash" is NOT "DSP is correct" (see verification gap)

  @in-chain @ref-designed @untested
  Scenario: The 4-band parametric EQ shapes the spectrum (Orfanidis biquads)
    # cite: ParametricEqDesign.h:1 makeOrfanidisPeakCoefficients (sinh-mapped digital BW, peq.m)
    # cite: PluginProcessor.cpp:1909 lowShelf, :1917 Mid1, :1923 Mid2, :1924 highShelf
    # cite: params lowCf/lowGain, mid1*, mid2*, hiCf/hiGain (createParameterLayout 328-386)
    Given a band's centre/bandwidth/gain are set
    When a signal passes the low-shelf → Mid1 → Mid2 → high-shelf chain
    Then each band boosts/cuts per the Orfanidis peaking design
    # ref-designed against Orfanidis peq.m; NOT verified against it (no magnitude-response test run)

  @built @untested
  Scenario: "Linear EQ only" gives a clean reference path
    # cite: PluginProcessor.cpp:546 linearEqListen param; :1787 the if(linearListen) branch
    Given linearEqListen is enabled
    When audio is processed
    Then only the linear IIR EQ (+ APR/ParEx) runs — saturation/roast/shapers are bypassed
    # the intended A/B reference; bypass-correctness not asserted by any test

  @in-chain @fixtures-only @caveat
  Scenario: ThrillMe tone stages add spectral emphasis + multiband dynamics
    # cite: ThrillMeTone.h:99 processChannel (4 RBJ filters + 3-band comp + tanh limiter)
    # cite: PluginProcessor.cpp:1879 thrillMe1 (pre-EQ), :1948 thrillMe2 (post-EQ)
    Given ThrillMe 1/2 are engaged with spectral/threshold/ratio set
    When a signal passes through
    Then it gets shelf/peak emphasis, 3-band compression, and a tanh limit
    # @fixtures-only: thrillme_fixtures/ has 7 deterministic inputs BUT no reference
    #   captures and no comparison run. @caveat: the reverse-engineering is explicitly
    #   INCOMPLETE (README: crossover corner / Q / threshold curve not pinned down).

  @in-chain @ref-designed @untested
  Scenario: The VA nonlinear SVF adds a saturating bandpass resonance
    # cite: VaNonlinearSvf.h:20 processBandpassNonlinear (trapezoidal ZDF, tanh in loop)
    # cite: PluginProcessor.cpp:1927 vaSvfPerChannel.processBandpassNonlinear
    # cite: ORACLE EXISTS but unused as a test: PluginProcessor.cpp:71-93 svfLinearBandpassTransferHz
    Given svfEnable on with cf/Q/drive set
    When a signal passes the resonator (parallel, post high-shelf)
    Then a tanh-limited bandpass resonance is mixed in
    # an analytic oracle for the linear case EXISTS (71-93) but is NOT run as a regression check

  @in-chain @untested
  Scenario: Roast core saturation/crunch colours the signal
    # cite: PluginProcessor.cpp:1906 applyCoreSaturation; roastCoreShape choices (459-466)
    # cite: low-chain tap :1915, mid-chain tap :1921 (nonlinearity inside the topology)
    Given coreDirt/coreCrunch/roastCoreShape set
    When audio passes the core stages (incl. inter-stage taps)
    Then it is saturated per the chosen shape (Classic/Warm/Aggro/Tape/Punch)
    # no distortion-spectrum / THD verification

  @in-chain @untested
  Scenario: Paketti Shaper waveshapes via Magnet or a Chebyshev LUT
    # cite: PluginProcessor.cpp:1882-1902 shaperEngaged (Magnet 1890, Chebyshev 1896)
    # cite: PakettiShapers.h:220 magnetProcessSample; :80 rebuildChebyLut (4096-pt)
    # cite: ChebyLutBuilder.{h,cpp} background double-buffered LUT rebuild + sync fallback
    Given shaperMode = Magnet or Chebyshev with its controls set
    When a signal passes the shaper (pre-EQ position)
    Then it is reshaped by the asymmetric Magnet saturator or the Chebyshev harmonic LUT
    # LUT rebuild glitch-freeness and harmonic accuracy are unverified (ear only)

  @in-chain @ref-designed @untested
  Scenario: The anharmonic partial bank adds stiff-string overtones
    # cite: ParametricEqDesign.h:62 stiffStringPartialHz f_n = n*f0*sqrt(1+B*(n²-1))
    # cite: PluginProcessor.cpp:1936 anharmPeakPerChannel[p].processSample
    Given anharmBankEnable with fundamental/inharmonicity B/partials set
    When a signal passes the partial bank
    Then resonant peaks are placed at inharmonic (stretched) partial frequencies
    # stiff-string formula is standard but partial placement is not measured

  @in-chain @untested
  Scenario: Auto-Parametric Resonator (APR) adds envelope/pump-modulated resonance
    # cite: AutoparametricResonator.h:33 process (env-track + sinusoid FM on cf)
    # cite: PluginProcessor.cpp:1961 aprResonator.process (parallel mix)
    Given aprEnable with baseHz/Q/pump/autoTrack/drive set
    When a signal passes APR in parallel
    Then a bandpass whose centre is modulated by envelope + pump is mixed in
    # stability/behaviour across the parameter range is unverified

  @in-chain @ref-designed @untested
  Scenario: Parametric Excitation (ParEx) pumps sub-octave energy (Mathieu)
    # cite: ParametricExcitation.h:44 process (cf modulated at rational ratio of f0)
    # cite: PluginProcessor.cpp:1967 parexResonator.process (parallel mix)
    Given parexEnable with baseHz/Q/ratio/depth/drive and pump source set
    When a signal passes ParEx in parallel
    Then parametric pumping injects energy near the Mathieu instability tongue
    # ref-designed on the Mathieu equation (depth clamped to ~1.6/Q); injection not measured

  @built @untested
  Scenario: Motion/LFO modulates EQ bands (free or host-synced)
    # cite: createParameterLayout lfo* params (819-863): per-band rate/depth, host sync div
    Given Motion is engaged on a band (rate/depth, or host-sync division)
    When the transport runs
    Then the band's gain/cf/bw is modulated by the LFO
    # no test that modulation depth/sync matches the set values

  @in-chain @untested
  Scenario: Output limiter and master dry/wet finish the chain
    # cite: PluginProcessor.cpp:2104-2116 outputLimiter; :2129-2141 masterDryWet crossfade
    Given outLimOn with threshold/release, and a dry/wet setting
    When the processed signal reaches the output
    Then it is limited (if on) and crossfaded with dry per masterDryWet
    # limiter threshold/release behaviour not measured

  @built @untested
  Scenario: 15 factory programs recall full parameter states
    # cite: PluginProcessor.cpp:2772 getProgramName; kNumFactoryPrograms=15 (PluginProcessor.h:36)
    Given the user selects a factory program (Init … APR foley)
    When the program loads
    Then the full parameter set for that program is applied
    # @caveat: host program-index sync after session recall is a known TODO

  @built @untested
  Scenario: Per-block one-pole smoothing kills knob-move clicks
    # cite: PluginProcessor.cpp:946-948 ~20ms block coeff; :1622-1631 per-band smoothing step
    Given a band/shaper parameter is moved during playback
    When the next block is processed
    Then the parameter eases to target over ~20ms (no zipper click)
    # audible click-freeness not asserted; smoothing is present in code

# ── VERIFICATION GAP (the honest spine) ──────────────────────────────────────
# thrillme_fixtures/ has 7 deterministic input WAVs + compare_wavs.py, BUT:
#   - NO reference captures from the original ThrillMe (captures/ empty)
#   - compare_wavs.py prints RMS/peak/correlation with NO tolerance, NO pass/fail
#   - NO renderer pipes inputs through the plugin; NO CI; NO assertions in the audio path
# ⇒ EVERY DSP scenario above is @untested in the strict sense: it is BUILT and runs,
#   the design often follows a cited reference, but NOTHING measures the output.
#   The plugin is ship-ready by ear, scientifically unverified.
#
# ── FOUR PROPERTIES ──────────────────────────────────────────────────────────
# P1 verifiable claims : ✓ (this card)   P2 linked to innards : ✓ (cites above)
# P3 honestly graded   : ✓ (@untested is the dominant grade, on purpose)
# P4 two-way back-link  : ✗ — Source/*.{h,cpp} carry NO "// FEATURE-CARD >>" marker yet
# Triad: .feature partial (this) · .session MISSING · RESULT present ⇒ INCOMPLETE unit.
