# ParaEQ / filter research — TODO

- [ ] **RBJ baseline:** Keep cookbook-style biquads (JUCE `IIR::Coefficients` = RBJ family) as the predictable linear reference; always have a clean bypass or “linear” comparison path.  
  **Done in-plugin:** `linearEqListen` (“Linear EQ only”) on the **Roast** tab = linear IIR chain only (+ trim + limiter), no cores / roast DSP.

- [ ] **Deliberate imperfection “in the middle”:** Experiment with nonlinearity *inside* the topology (between biquad stages, on feedback, or on filter state) — not only output `tanh` or only pre-EQ saturation.  
  **Done in-plugin:** `roastLowChain` (after low shelf, before Mid1) and `roastMidChain` (between Mid1 and Mid2) core taps.

- [ ] **Butter-style sibling (optional):** N-pole Butterworth-style ladder + sigmoid distributed in the chain (vs Renoise-style output-only saturation); likely a **separate** plugin or mode, not a bolt-on to the parametric EQ layout.

- [ ] **Self-oscillation:** If wanted later, treat as its own feature — controlled feedback + limiting in the loop; not the same as “more drive” on linear RBJ sections.

## Crunch / roast roadmap

**Shipped:** Roast tab DSP (crunch, shelves, punch/glue, lo-fi, ring, env drive, flutter, stereo life, trim), **motion-aware** `roastBoostTrack` (uses `motionUi*` snapshot when Motion is engaged), **low-chain** tap, **linear EQ A/B**, **6 factory programs** (`getProgramName` / `setCurrentProgram` + Roast tab combo), **nonlinear SVF VA resonator** (`VaNonlinearSvfChannel`: trapezoidal ZDF SVF bandpass, `tanh` loop input, parallel after high shelf before Core 2; controls on Roast tab).

**Still future:** dedicated **sidechain input bus**; **Butter-style ladder** as separate plugin/mode; **host program index** sync for the factory combo after session recall.
