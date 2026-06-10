# Session — BPM-sync divisions + rate-jitter LFO + Shaper draw-grid + UI fixes

Spawning conversation for `bpm-sync-jitter-and-ui-fixes.feature`. Faithful, not flattering.

## How to get back
- Transcript: `file:///Users/esaruoho/.claude/projects/-Users-esaruoho-work-paraEQ/117fb062-3779-4c78-8078-a840678b15b4.jsonl`
- Session ID: `117fb062-3779-4c78-8078-a840678b15b4`
- Resume: `claude --resume 117fb062-3779-4c78-8078-a840678b15b4`
- Date: 2026-06-10 (EEST). Model: Claude Opus 4.8 (1M).

## Requests, in order
1. Spectrum legend text clashed with the "1k" freq-axis label → "move it to the top."
2. Master Dry/Wet caption should be centred under the slider track (not the track+value-box column).
3. BPM Sync: add 4/8/16/32/64 bars; put each triplet right below its base (1/16 → 1/16 triplet);
   add 1/16 dot; fastest at top, slowest at bottom; "add a LFO that can change between them."
4. "make a gherkin feature for all of these … tested and built before you say done … then deploy
   to github repo, make sure action is built, and the build is deployed to gumroad."

## Decision points
- **AskUserQuestion — the "LFO that can change between them":** offered (a) sweep between two
  divisions, (b) auto-step through divisions, (c) rate jitter around one division. **User chose (c)**:
  "Rate jitter around one division" — keep one division, an LFO morphs the rate between half-time
  and double-time. Adds enable + depth + rate.

## Corrections during the session (the honest audit trail)
- After first cut, user: *"the sliders are too small. they should be i think 6x wider. and the
  sliders you added dont work."*
  - Too small: they were crammed onto the shared EQ Motion row. Fix → moved to a dedicated
    full-width row (syncJitterRowH) with two wide sliders.
  - Don't work: they were `setEnabled(bpmSyncOn && jitterOn)` so greyed/dead with sync off (and the
    Standalone supplies no host tempo, so sync itself does nothing there). Fixes → (1) controls are
    always interactive; (2) the jitter factor now multiplies the effective Motion rate in BOTH the
    synced and free-running cases, so it is audible/visible even in the Standalone.
- Then user: *"the Shaper H2-H12 and H2Pow-H12Pow should work … like a grid that i can draw to …
  so i dont need to click 12 times … i can just draw it."* → added HarmDrawStrip transparent
  overlays over both harmonic rows (x→column, y→value, one sweep sets all 12). Labels above and
  value boxes below stay editable.

## Side effects surfaced
- Reordering the `lfoHostSyncDiv` AudioParameterChoice remaps stored indices: projects saved before
  this change may recall a different division. Accepted (the re-ordering was requested). Default
  index moved 3→6 to keep "1/8" as the default.
- Standalone has no transport tempo, so BPM-sync rate itself is inert there; documented.

## Verification
- `./start.sh` clean (AU + VST3 + Standalone), 2026-06-10.
- UI snapshot confirmed: legend at top, Dry/Wet caption centred, BPM-sync default 1/8, jitter on its
  own wide row. (dist/shots/paraeq-ui-20260610-*.png)
- Sync-division math checked by hand (beatsPerCycle). Shaper overlay geometry checked against
  placeVertCol (label 14 + track + TextBoxBelow 18 = kColH 110 → track 78px).
- Jitter mouse-drag and Shaper draw-drag are @built-runs + logic-verified; no automated UI oracle.
