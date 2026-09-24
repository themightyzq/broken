# PITFALLS — build-time checklist, tailored to this design

> The the prototyping environment-era version of this project is retired; its conventions (Order modules,
> Core/Primary, DN Cancel, snapshot isolation) survive only as history in DSP-NOTES §0.
> This checklist tracks the hazards that have actually bitten the C++/JUCE engine. Tick
> per feature before calling it done.

## Unclamped modulator sources (v0.12)
- [ ] **Any signal read as a buffer index** (modulator read pointer, wavetable lookup):
      clamp at the source, not just at the read site. `Modulator::applyPM` walked its read
      index off the buffer whenever the modulator ran hotter than its documented ±1 —
      reachable via the v0.7 Self/Sample modulator sources, and nondeterministic (same
      seed, different results run to run). Fixed by clamping the modulator signal to ±1 in
      `Voice::render` plus a defensive bounds check in `applyPM`. Caught by `--fuzz` + ASan,
      not by confirmatory testing — check any new modulator/wavetable source the same way.

## Async callbacks capturing raw `this`
- [ ] **FileChooser / AlertWindow lambdas**: a host can close the editor while the async
      callback is still pending, so a raw `this` capture is a crash waiting to happen.
      Bit tape SAVE and sample load in v0.12; recurred and was fixed again in v0.23.
      Guard every async UI callback with `Component::SafePointer`, not a raw pointer.

## GUI thread racing state restore
- [ ] **`setStateInformation`**: hosts do not guarantee it runs on the message thread, but
      the sample reload it triggers touches GUI-owned state. Marshal the reload to the
      message thread explicitly — don't assume the callback thread is safe to touch GUI
      state from.

## Hand-formatted parameter-id strings
- [ ] **Indexed parameter banks** (curve points, draw points, harmonics): formatting an id
      as a literal `"ws.c%02d"`-style string in more than one file lets a renumbering
      (v0.20) orphan callers the compiler can't see — `COPY→CUSTOM` went silently dead this
      way (v0.20/v0.21). `Params.h` now owns every banked id and count; go through it, never
      re-derive an id string locally. Guarded by `broken_cli --param-check` (every generated id
      must resolve to a real parameter) and `scripts/check_ids.py` (v0.23: no hand-formatted
      bank ids exist outside `Params.h`).

## Hoisting code without re-reading what follows it
- [ ] **Moving a block earlier in a function** (e.g. the MIDI parse hoisted above
      `applyParams` for pitch bend, v0.22): check what used to run right after the old
      location. The original `events.clear()` was left sitting after the hoisted parse,
      which would have wiped every real note before `engine.process` — all MIDI input
      dead. Diff the surrounding lines, not just the moved block.

## Comments asserting behavior that was never coded
- [ ] **A comment claiming "X is reset in Y"**: verify the reset actually exists.
      `pitchBendNorm` was documented as reset in `prepareToPlay`, and the reset was never
      written (v0.22) — a wheel held at a transport stop would have stranded the
      instrument detuned. Grep for the described behavior; don't trust the comment.

## Harness limits masquerading as product bugs
- [ ] **Before filing a regression, rule out the test harness.** `broken_cli`'s MIDI reader
      flattened files to note-on/off only and silently dropped pitch-wheel messages, which
      first made bend look like it did nothing (v0.22, fixed by `TimedMsg` carrying the raw
      `juce::MidiMessage`). Separately, running the M8 tuning gate against a *mono* render
      showed a 238-cent "failure" when TEST-PLAN documents that check as poly-only — mono
      collapses chords to the last note (v0.22, harness error, not a regression). When a
      measurement looks impossible, check what fed the measurement first.

## Denormals
- [ ] **Every exponentially-decaying recursive path** (filter stages, comb/resonator
      feedback, delay feedback, DC blocker): nudge by ±1e-18 or otherwise force flush-to-
      zero. Verify with a silence-through-full-chain render — CPU must not spike after the
      tail (measured clean at v0.12: 9 s silent tail ran *faster* than a held note).

## Audio-thread hygiene
- [ ] **No allocation, no locks, no string formatting on the audio thread.** Applies to
      every new DSP class and every callback reachable from `processBlock`. Verified
      periodically (e.g. v0.21: longest parameter id is 17 chars, inside libc++'s 22-char
      SSO, so id lookups don't allocate).

## Docs vs. code drift
- [ ] **Docs are updated BEFORE code**, per this project's CLAUDE.md. When implementation
      forces a deviation from DSP-NOTES.md or PANEL.md, fix the doc first, with the reason,
      then the code — not after, and not "later."
