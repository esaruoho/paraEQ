# ParaEQ / filter research — TODO

- [ ] **RBJ baseline:** Keep cookbook-style biquads (JUCE `IIR::Coefficients` = RBJ family) as the predictable linear reference; always have a clean bypass or “linear” comparison path.
- [ ] **Deliberate imperfection “in the middle”:** Experiment with nonlinearity *inside* the topology (between biquad stages, on feedback, or on filter state) — not only output `tanh` or only pre-EQ saturation.
- [ ] **Butter-style sibling (optional):** N-pole Butterworth-style ladder + sigmoid distributed in the chain (vs Renoise-style output-only saturation); likely a **separate** plugin or mode, not a bolt-on to the parametric EQ layout.
- [ ] **Self-oscillation:** If wanted later, treat as its own feature — controlled feedback + limiting in the loop; not the same as “more drive” on linear RBJ sections.
