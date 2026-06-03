# =============================================================================
# BUYER'S CARD (sales skin): ParaEQ 301 — what you'll HEAR, graded by your ears
# Companion to: features/paraeq.feature (engineer's card) · features/paraeq.report.md
# Built with: rhythmic-balanced-interchange + marketing-psychology skills.
# Sale model: direct purchase on Gumroad — NO demo build.
#
# Each Scenario = one fulcrum (one thing the listener verifies). Three max — the
# RBI "drop" discipline, not a feature-list cloud. Claims are about EXPERIENCE
# (tone / feel / movement), never measured fidelity — the engineer's card grades
# the DSP @untested, so this card never says "verified / accurate / reference-grade".
#
# Tags = the marketing model carrying each scene (Voltage Ladder — never stacked):
#   @jobs-to-be-done @contrast   — you buy the sound, not the EQ
#   @pratfall @two-way-test       — it lets you switch its own colour OFF
#   @peak-end                     — the screenshot-and-send-a-friend moment
#   @activation-energy @regret-aversion — the CTA's low first step + refund safety
# =============================================================================

Feature: ParaEQ 301 — a parametric EQ that finishes the sound and makes it move
  As someone mixing a record,
  I want EQ that not only corrects frequencies but adds weight, character and movement,
  So that the track sounds finished and alive — from one plugin I stay in control of.

  @jobs-to-be-done @contrast
  Scenario: EQ that finishes the sound, instead of just correcting it
    Given my EQ gets the frequencies right but the track still sounds flat and digital
    When I shape it in ParaEQ — the same parametric bands, but with its saturation and shaper stages in the chain
    Then the move that used to sound "corrected" now sounds "finished": weight and warmth, not just a curve
    # You're not buying an EQ — you're buying the sound. Contrast: corrected -> finished.

  @pratfall @two-way-test
  Scenario: Nothing hidden — A/B the colour against clean, keep only what serves the song
    Given I don't trust "character" plugins that smear the mix and hide behind hype
    When I flip "Linear EQ only" and compare the clean parametric against the full chain
    Then I hear exactly what ParaEQ adds, and decide by ear — no faith required
    # A plugin confident enough to let you mute its own colour earns trust. Two-way test passed.

  @peak-end
  Scenario: Make a frequency breathe — the thing a normal EQ cannot do
    Given a static EQ can boost a band but can never make it move
    When I bring up the resonator cores and Motion — saturating bandpass resonance, parametric pumping, an LFO across the bands
    Then a flat element starts to move and sing in a way ordinary EQ simply can't
    # The peak: the moment you screenshot and send a friend. The reason to own THIS, not the stock EQ.

  @activation-energy @regret-aversion
  Scenario: Buy it, prove it on a real mix, keep it only if it earns its place
    Given ParaEQ 301 is available now as a direct purchase on Gumroad
    When I buy it, run it on a track I know, and reach for "Linear EQ only" first
    Then I decide by ear how much colour earns its place — and if it earns none, a refund is one click away
    # Risk is the seller's, not the buyer's. Regret-aversion placed on the dead center (the buy decision).
    # CTA: https://lackluster.gumroad.com/l/joucal  ·  EUR 135.40
    #      (one buy, one mix, one A/B — lowest first step without a trial build)

# ── PUBLISHING STATUS (2026-06-03) ───────────────────────────────────────────
# Gumroad product EXISTS: "ParaEQ" · EUR 135.40 · https://lackluster.gumroad.com/l/joucal
# Still a DRAFT (published=false) — the buy link is not live until you publish it
# (gumroad products publish). The "Available now" copy goes true the moment you do.
# Keep Gumroad refunds ENABLED so the "refund is one click away" line stays true.
#
# ── RBI BALANCE (why this is two-way, not manipulation) ──────────────────────
# Give-first ...... no demo, so the gift = the honest refund window + the in-plugin
#                   clean A/B; the buyer is never locked into colour they can't undo.
# Two-way test .... "Linear EQ only" switches the product OFF; refund keeps the
#                   exchange fair even after money changes hands.
# Drop discipline . one fulcrum per scenario, three claims + one CTA, no cloud.
# Honesty ......... experience-claims only; DSP fidelity is @untested (engineer's card).
