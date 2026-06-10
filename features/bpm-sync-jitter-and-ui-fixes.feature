# =============================================================================
# REPORT CARD: BPM-Sync divisions + rate jitter LFO + two UI alignment fixes
# Skin: mixed (DSP claim = input → stage → measurable output; UI claim = layout)
# Convention: ~/.claude/skills/report-card/SKILL.md
# SESSION >> features/bpm-sync-jitter-and-ui-fixes.session.md  (spawning conversation)
# RENDER  >> (this .feature is the human-readable view)
#
# ── WHAT THIS CARD SPAWNS ───────────────────────────────────────────────────
#   Codespace : Source/PluginProcessor.{h,cpp} (sync division list + beatsPerCycle
#               table + jitter params + jitter application in processRoastAndEqBlock),
#               Source/PluginEditor.cpp (spectrum legend paint, Dry/Wet caption
#               layout, BPM-sync jitter UI controls on the EQ motion row).
#   Thinkspace: features/bpm-sync-jitter-and-ui-fixes.session.md
#   Areaspace : OWNS the BPM-sync division mapping, the sync-rate jitter LFO, and
#               the two named UI alignments. MUST NOT change per-band LFO depth
#               semantics, the EQ/saturation chain, or unrelated layout.
#
# ── report-card legend (grades in use) ──────────────────────────────────────
#   @built-runs   - compiles to AU/VST3/Standalone, runs without crash (2026-06-10)
#   @ui-verified  - confirmed in a Standalone UI snapshot (dist/shots/, 2026-06-10)
#   @in-chain     - wired into processRoastAndEqBlock at a cited line
#   @math-checked - numeric mapping checked by hand against the musical definition
#   @untested     - no automated audio oracle / assertion yet
#   @caveat       - known sharp edge, stated honestly
#
# ── innards cited (file:line) ───────────────────────────────────────────────
#   PluginProcessor.cpp  createParameterLayout(): lfoSyncDivItems + 18-entry list,
#       default index 6 (=1/8); lfoSyncJitterEnable/Depth/RateHz params.
#     motionLfoHzFromSyncDivision(): beatsPerCycle[18] table (indices lock-step).
#     processRoastAndEqBlock(): jitter applied in the haveBpm branch (syncJitterPhase).
#   PluginProcessor.h    syncJitterPhase member (0..1 LFO phase).
#   PluginEditor.cpp     paintMergedSpectrumAndEqInRect(): legendStrip at TOP;
#     EqTabContent: lfoSyncJitter{Toggle,Depth,Rate} setup + motion-row layout;
#     resized(): masterDryWetCaption centred under the slider track only.
#
# ── RESULT (third leg) ──────────────────────────────────────────────────────
#   Feature delivery : direct-push to esaruoho/paraEQ main (this session).
#   Build artifacts  : build/ParaEQ301_artefacts/Release/{AU,VST3,Standalone} (2026-06-10).
#   CI               : .github/workflows/build-macos-plugins.yml (push→universal build+release).
#   Gumroad          : release.conf → convey release engine (zip upload + publish).
# =============================================================================

Feature: BPM-sync divisions, rate jitter LFO, and two UI alignment fixes
  As someone using ParaEQ's Motion LFO synced to host tempo,
  I want a fuller, musically-ordered division list with a rate-jitter LFO,
  and two legibility fixes (spectrum legend, Dry/Wet caption),
  So that tempo-locked motion is more expressive and the UI reads cleanly.

  @built-runs @ui-verified
  Scenario: Spectrum legend no longer collides with the frequency-axis labels
    # cite: PluginEditor.cpp paintMergedSpectrumAndEqInRect — legendStrip = plot.removeFromTop(11)
    # was: legend drawn at graph.getBottom()+1, overlapping the 100/1k/10k labels
    Given the main spectrum/curve panel is drawn
    When the legend "Spectrum (white pre-EQ / blue post) + 4-band IIR" is rendered
    Then it is drawn in a reserved strip ABOVE the graph
    And the 100 / 1k / 10k frequency labels remain below the graph
    And the two no longer overlap

  @built-runs @ui-verified
  Scenario: Dry/Wet caption is centred under the slider track
    # cite: PluginEditor.cpp resized() — caption width = sliderW - 52px value box
    Given the top-strip master Dry/Wet slider has a 52px right-hand value box
    When the "Dry/Wet" caption is laid out
    Then it is centred under the slider TRACK only
    And not under the combined track+value-box column

  @built-runs @math-checked
  Scenario Outline: BPM-sync division maps to the correct LFO cycle length
    # cite: PluginProcessor.cpp beatsPerCycle[] — quarter note = 1 beat, bar(4/4) = 4 beats
    Given host tempo is <bpm> BPM and BPM sync is on
    When the sync division is "<division>"
    Then one Motion LFO cycle spans <beats> quarter-note beats
    And the LFO rate is bpm / (60 * beats) Hz

    Examples:
      | division     | beats  | bpm |
      | 1/16         | 0.25   | 120 |
      | 1/16 triplet | 0.1667 | 120 |
      | 1/16 dot     | 0.375  | 120 |
      | 1/8          | 0.5    | 120 |
      | 1 bar        | 4.0    | 120 |
      | 4 bars       | 16.0   | 120 |
      | 64 bars      | 256.0  | 120 |

  @built-runs @ui-verified
  Scenario: Division menu is ordered fastest→slowest with triplets grouped after their base
    # cite: PluginProcessor.cpp lfoSyncDivItems (18 entries), default index 6 = "1/8"
    Given the BPM-sync division menu
    Then the entries read top→bottom fastest→slowest
    And each straight value is immediately followed by its triplet where one exists
    And "1/16" is followed by "1/16 triplet" then "1/16 dot"
    And the list extends through 4, 8, 16, 32 and 64 bars
    And the default selection is "1/8"

  @built-runs @in-chain @untested
  Scenario: Rate jitter LFO morphs the effective Motion rate between half-time and double-time
    # cite: PluginProcessor.cpp processRoastAndEqBlock — applied AFTER the sync block to all four
    #       band rates: factor = 2^(depth*sin(2*pi*syncJitterPhase)); phase += rateHz*numSamps/sr
    Given "Jitter" is enabled with depth 100% and some rate Hz
    When the effective Motion rate is hz (synced division rate, or the free per-band Hz)
    Then the effective rate oscillates between 0.5*hz and 2.0*hz
    And at depth 0% the effective rate equals hz exactly (no jitter)
    And it applies whether BPM sync is on (around the division) or off (around the Hz sliders)
    And so it is audible/visible even in the Standalone, which supplies no host tempo

  @built-runs @ui-verified
  Scenario: Jitter controls live on a dedicated full-width row with wide sliders
    # cite: PluginEditor.cpp — syncJitterRowH row below the Motion row; depth+rate split the width
    Given the EQ tab below the Motion row
    Then a "Jitter" toggle plus a wide depth % slider and a wide rate Hz slider occupy their own row
    And the depth and rate sliders are roughly 6x wider than a cramped inline pair
    And all three controls are always interactive (the toggle gates the effect, not usability)

  @built-runs @in-chain
  Scenario: Shaper harmonic rows are a draw-grid (one sweep sets all 12)
    # cite: PluginEditor.cpp ShaperTabContent::HarmDrawStrip — transparent overlay over each row;
    #       x -> column H2..H13, y -> value via proportionOfLengthToValue; sets the attached slider
    Given the Shaper tab in Chebyshev mode showing the H2-H13 and H2-H13 pow rows
    When I click and drag horizontally across a harmonic row
    Then each column under the cursor is set from the cursor height in one gesture
    And I do not have to click each of the 12 sliders individually
    And the labels above and the value boxes below each row stay visible and editable

  @caveat
  Scenario: Reordering the division choice changes saved automation index meaning
    # AudioParameterChoice stores an index; the new ordering remaps old indices.
    Given a project saved before this change with a stored lfoHostSyncDiv index
    Then the recalled division may differ, because the choice list was reordered
    And this is an accepted consequence of the requested musical re-ordering
