# PANEL — Broken (ZQ SFX)

> **Visual spec of record (v0.27): `ClaudeDesign/design_handoff_broken_ui/README.md`.**
> The panel implements that handoff — "degraded 90s rackmount sampler": rack-metal
> chassis, teal silkscreen labels, green VFD/LCD readouts, filmstrip analog knobs
> (the ZQ SFX house knobs since 2026-09-21: three CC0 designs embedded by the shared
> `zqsfx_ui` module), orange LEDs, grain/vignette grime, corner screws. Fonts:
> Barlow Condensed (silkscreen), VT323 (LCD values), IBM Plex Mono (serial stamps) —
> all OFL, embedded. Where this file and the handoff disagree on LOOK, the handoff wins;
> where they disagree on BEHAVIOUR, this file and DSP-NOTES win.
>
> **v0.27 layout regroup (same controls, same parameters, same behaviour):** design size
> 1520 px wide. Header: logo · REV.C/SN stamp · preset cluster. Top row: SOURCE (300) ·
> MANGLE (620, all rows share one 4-column grid so knobs align; the four big knobs carry
> LCD readouts) · right column: PLAY and TAPE side by side — **RETRIGGER and BEND now
> live in PLAY** — with a horizontal OUTPUT beneath, and TAPE gains a TAKE readout.
> Bottom row: **ENVELOPES** (FLT + AUX rows aligned on one grid, FLOOR EXT and DEST at
> the row ends) · **OSCILLATOR** (harmonics display, OSC MODE/OSC WAVE, SAW/SQR/FLAT,
> XFADE, PITCH MIX, PITCH EXT, LOOP XFADE) · **WAVESHAPER** (curve display, FROM SAMPLE,
> TRIM, RND, COPY TO CUSTOM) · **MODULE TRIMS** spanning two columns (FM · DELAY ·
> RESONATOR · INVERT type · NOISE segments) · **TIME**. Footer serial stamps. The twelve
> EDIT blocks of v0.22–v0.26 no longer exist as blocks; their rows in this file describe
> the CONTROLS, which are unchanged.
>
> **Tooltips: every interactive control has one (v0.27),** including the source window,
> which now owns its own tooltip display.

> Defaults below ARE the `00 Init` snapshot — they are chosen deliberately so that loading
> a sample and turning DRIVE is already "a sound." Every control gets info text (release
> requirement); the info-text draft is the last column. Ranges/units per DSP-NOTES.md.

## Make a sound in 30 seconds

Load the plugin: it opens in SAMPLE mode with nothing loaded, so the display reads "DROP
SAMPLE or CLICK." Either drop a WAV on it, or switch SOURCE to OSC or NOISE if you don't
have one handy. Press PLAY — no MIDI keyboard needed, it latches a note at C3 and leaves
both hands free for the knobs. Turn DRIVE.

> Quicker still: the preset bar's **01 start here** (saw through the shaper and resonator)
> and **02 start here noise** (pitched noise, crushed and echoed) both sound with nothing
> loaded — press PLAY and turn DRIVE. Every other factory preset expects a sample.

## MANGLE — the top half (the user-friendly face)

One window, opening at 1520×1024 and scaling as a unit from 55% to 175% (aspect ratio
locked); the toggle that used to swap MANGLE and EDIT is gone, and the EDIT controls now
sit in five panes beneath MANGLE (ENVELOPES / OSCILLATOR / WAVESHAPER on one row, MODULE
TRIMS / TIME below). Goal: an industrial sound in three moves — pick a
source, pick a curve, turn a big knob. Layout left→right mirrors signal flow.

### Source block

> Context default (v0.33): a DAW instance (VST3/AU) opens in **Input** — it sits on a
> track, so it listens to the track. Standalone opens in **Sample**. Sessions and
> presets override.
| Control | Type | Range | Default | Info text draft |
|---|---|---|---|---|
| SOURCE | selector | Sample / Cycle / Osc / Noise / Input / Tape | Sample | What gets mangled. CYCLE loops a tiny slice of your sample as a raw oscillator — try a real low note. |
| (waveform display) | display | — | — | Shows the loaded sample, the region (tinted band), and the cycle window. Double-click or hit EDIT to open the region editor. |
| EDIT (region editor) | button → overlay | — | — | Zoom, pan, and drag-select the part of the file you want — no destructive editing. Play a MIDI key to hear it live while you drag. |
| LOOP | toggle (editor) | on/off | off | Off = one-shot. On = the region repeats while you hold the key; style and XFADE light up. |
| STYLE | selector (editor) | Loop / PingPong | Loop | Wrap back to the start, or bounce end-to-end. |
| REV | toggle (editor) | on/off | off | Reverses the source, Soundminer-style. Works with everything — one-shots, loops, bounces. |
| XFADE | knob + ms readout (editor) | 0–250 ms | 10 ms | Smooths the loop seam and the ping-pong turnaround. 0 = the raw era click. Shown as a shaded wedge on the waveform. |
| ZOOM SEL | button (editor) | — | — | Frames the current selection for loop-point surgery. |
| SNAP | toggle (editor) | on/off | on | Snaps selection edges to zero crossings on release. |
| PLAY | button (latch) | — | off | Plays the sound without a MIDI keyboard, and leaves both hands free for the knobs. Click again to stop. Pitch comes from PITCH/FINE. |
| PITCH | knob | ±24 st (EXT: ±48) | 0 | Transpose. Pitching down also slows — that's the point. In Input mode this pitches the live signal (tape-head varispeed — warble included). |
| FINE | knob + ¢ readout | ±50 cents, 1¢ steps | 0 | Fine tune, same range as a pitch-shifter's fine control. TUNE sets it for you. |
| TUNE | button | — | — | One press: locks the source to the nearest note using the IN tuner. Dimmed when no confident pitch. |
| IN tuner | display | note ±50¢ + Hz | — | What's coming in — sample, osc, or live input — before the mangle. |
| OUT tuner (Output block) | display | note ±50¢ + Hz | — | What's coming out after everything. Chase it with FINE if you want the wreckage in tune. |
| WINDOW / POS | 2 knobs | pos 0–1, len 32–4096 smp | 0.1 / 512 | CYCLE only: where and how much of the sample becomes the oscillator. |
| IN TRIM | knob | ±12 dB | 0 | Input mode: level into the chain. |

### Mangle block (one big knob per module + on/off light per module)
| Control | Type | Range | Default | Info text draft |
|---|---|---|---|---|
| MOD | big knob | 0–1 | 0 | Death-vocal machine: multiplies the sound with a low bell tone. Turn up, then tune MOD FREQ. |
| MOD FREQ | knob | 0.1 Hz–2 kHz log | 65.41 Hz (C2) | Frequency of the modulating tone. Low = growl, high = metallic. Defaults to a C so it agrees with the C3 source root. |
| MOD MODE / WAVE | 2 selectors | AM/RM/FM · sine/bell/odd | RM · bell | How and with what the sound is modulated. |
| CURVE | selector (10) | curves 1–8 + Random + Custom | 3 Soft Sat | Click through the shaper curves one by one. 1 is clean. Random and Custom are edited in the EDIT view's CURVE block. |
| DRIVE | big knob | 0–40 dB | 12 dB | How hard the sound hits the curve. The main damage control. |
| MORPH | knob | 0–1 | 1 | Blend between clean and the selected curve. |
| FILTER | big knob | 500 Hz–20 kHz log (EXT: 20 Hz) | 20 kHz | Low-pass only, by design. POLES sets steepness. The knob's travel stops at the floor (500 Hz, or 20 Hz with FLOOR EXT) so there is no dead zone; switching EXT off with the cutoff below 500 pulls it up to 500. |
| POLES | selector | 1–4 | 2 | 6/12/18/24 dB per octave. |
| RES | big knob | 20 Hz–2 kHz log | 130.81 Hz (C3) | Ringing comb resonator pitch. Defaults to unison with the source root, so raising FB reinforces the sound instead of clashing with it. |
| RES FB | knob | −0.995…+0.995 | 0 | Ring length. Negative = hollower. |
| INVERT | knob | 0–1 | 0 | Spectral flip mix — mirrors the spectrum. Weird by design. |
| DELAY / MIX | 2 knobs | 0.1–2000 ms · 0–1 | 80 ms · 0 | Simple delay with polarity flip (⌀ button). |

> FILTER readout shows the *effective* cutoff: with FLOOR EXT off the DSP floors cutoff
> at 500 Hz (DSP-NOTES.md §4), so the readout never displays below 500 until EXT is on.
> The stored parameter keeps its raw value — toggling EXT reveals it, nothing is lost.

### Play block
| Control | Type | Range | Default | Info text draft |
|---|---|---|---|---|
| MONO/POLY | button | mono / poly | MONO | Mono = one brutal voice (the workflow). Poly = playable. |
| UNISON | button + spread knob | off/on · 0–50 ct | off · 12 | Stacks all 6 voices detuned on one note. |
| A / D / S / R | 4 knobs | 1 ms–10 s | 5 ms / 200 ms / 0.8 / 150 ms | Amp envelope. |

### Tape block (the resample workflow)
| Control | Type | Range | Default | Info text draft |
|---|---|---|---|---|
| REC | button (latch) | — | off | Records the output (max 60 s) into the tape. |
| FLIP | button | — | — | Makes the recording the new source. Mangle it again. Generations. |
| SAVE | button | — | — | Saves the LATEST take as a 24-bit WAV — no FLIP needed (v0.33). Dimmed while the tape is empty. |
| TAKE display | LCD + drag source | — | — | Shows the latest take's length. Drag it into a DAW/Finder to export the take as a WAV (v0.33). |
| TAPE light | indicator | — | — | Lit when TAPE is the active source. |

### Output block
| Control | Type | Range | Default | Info text draft |
|---|---|---|---|---|
| MIX | knob | 0–100 % | 100 % | Wet/dry. Dry = the un-mangled source (sample/osc/input), blended in before COLOUR and OUT — parallel mangling in every mode. |
| COLOUR | selector | Off / 12-bit / 8-bit | 12-bit | The sound of the sampler this would have been rendered to. |
| RATE | knob | 8–48 kHz | 44.1 kHz | Sample-rate crush. No smoothing filter, on purpose. |
| OUT | knob | −60–+6 dB | 0 dB | Master level. |
| BYPASS | button (latch) | — | off | True bypass: the track passes through untouched (~25 ms fade, no clicks). Hosts bind their own bypass control to it. |
| (meter) | display | — | — | Output level. |

## EDIT — the five panes beneath

Everything above plus the deep set: Filter Env ADSR + ENV AMT (±60 st), AUX ENV ADSR +
DEST (FilterCut/ResFreq/InvMix/ModAmt/Pitch) + AMT, CYCLE seam crossfade (0–16 smp),
FM index, delay FINE + feedback (flagged as non-original), retrigger toggle, EXT range
toggles (filter floor, pitch range), per-module hard-bypass buttons, waveshaper TRIM.

## Panel rules

- Big knobs = the five "moves": DRIVE, MOD, FILTER, RES, PITCH.
- Module on/off lights double as hard-bypass switches (true bypass — null-test requirement).
- No control without info text; info text is copy-edited at release, drafted at build time.
- Host automation lanes show real values with units ("2400 Hz", "-12.0 dB") for every
  unit-bearing float param (v0.34); 0..1 blends stay plain floats (a scaled display
  would break host type-in parsing).
- Panel must fit a 13" laptop screen without scrolling at the 55% minimum scale (55% of
  1520×1024 is 836×563).

## EDIT view additions (v0.7–v0.11)

| Control | Block | Range | Default | Info text |
|---|---|---|---|---|
| SRC (mod source) | MANGLE (MOD) | Osc / Self / Sample / Tape / Table | Osc | What modulates: the internal osc, the sound itself, the sample, the tape, or the OSCILLATOR panel's own shape (Table, v0.35 — drawn, harmonics or wave) — any module as modulator, by design. |
| MODE (mod) | MANGLE (MOD) | AM / RM / FM / PM | RM | How the sound is modulated. PM is the classic chorus/vibrato mode. |
| PITCH MIX | EDIT | 0–1 | 1 | Blend of pitched vs unpitched playback — a pitch-shifter mix control. ~50% with FINE detune = the classic chorus recipe. |
| CURVE editor + RND + COPY→CUSTOM + FROM SAMPLE | EDIT (CURVE) | — | — | The waveshaper's transfer curve. **Just drag on it** — that switches to CUSTOM and keeps the shape you were looking at (128 points, pencil not handles). RND rolls a new random curve (seed saved with the preset). **FROM SAMPLE** turns the CYCLE window of the loaded sample into the curve itself. |
| HARMONICS editor + MODE + SAW/SQUARE/FLAT | EDIT (HARMONICS) | 64 partials, 0–100% | saw (100/k) | The 64 partials of Harmonic Mode. Drag bars to draw; hold Shift to rake a straight line across them. |
| STRETCH + AMOUNT/FREQ/PREDELAY | EDIT (TIME) | on/off, ±100, 20–2000 Hz, 0–1000 ms | off, 0, 130.81 Hz (C3), 0 | Segment-repeat time stretch. Positive stretches, negative compresses. Tune FREQ to the material or enjoy the artifacts. |
| FLATTEN + RESP | EDIT (TIME) | on/off, 1–500 ms | off, 50 ms | Envelope Removal: levels out the sound's own dynamics, like heavy compression. |
| BEND | EDIT (VOICE) | 0–24 semitones | 2 | How far the pitch wheel bends. 2 is the usual, 12 is an octave dive. **0 switches the wheel off entirely.** On a sample the bend changes speed as well as pitch, exactly like the PITCH knob. |
| AMP NZ / PH NZ | EDIT (COLOUR/NOISE) | 0–100% | 25 / 25 | Noise source randomness. Both 0 = a plain sine; both 100 = white noise. |
| INV TYPE | EDIT (COLOUR/NOISE) | A / B | A | Spectral inverter flavour: A mirrors around Nyquist, B makes two quarter-rate images. |
| XFADE SHAPE | EDIT (COLOUR/NOISE) | Linear / EqPower | Linear | Loop crossfade shape. Equal-power holds the level through the seam. |

## Preset bar (header, v0.13)

| Control | Type | Info text |
|---|---|---|
| `<` / `>` | buttons | Step to the previous / next preset. |
| PRESET | selector | Factory presets are built into the plugin; your own saved ones appear under USER. A `*` means you've changed something since loading. |
| SAVE | button | Saves the current sound to your own preset (~/Library/Audio/Presets/ZQ SFX/Broken). |
| `...` | button | Preset actions menu (v0.34): reveal folder in Finder; overwrite / rename / delete the selected USER preset (factory rows are disabled; delete moves to Trash). |
| BROKEN logo | clickable | About overlay (v0.34): version, GPLv3 + source note, knob/font credits, non-affiliation. Click or Escape closes. |
| RND | button | Rolls the dice: randomizes the sound (v0.32). Leaves alone: your sample, source mode, output level, the tape transport, and hand-drawn curves. Feedback and drive are capped so it can never scream or damage anything; filter and attack are floored so it can never land on silence. |
| UNDO | button | Restores the exact state from before the last RND. Enabled only after a roll. |

> **Custom wavetable distortion: a sample can BE the distortion curve (v0.20).** In the
> EDIT view's CURVE block, **FROM SAMPLE** fills the waveshaper's 128-point CUSTOM curve
> from the CYCLE window of your loaded sample (centred, then peak-normalized). The
> sample's shape then decides how everything downstream gets mangled — input level looks
> up into the sample.
>
> **Expect scream, not saturation.** An audio slice is a *non-monotonic* transfer curve,
> so a clean sine goes in and broadband noise comes out: measured **THD −0.3 dB**, i.e.
> the harmonics are as loud as the note. That is the era-correct answer to "what if I draw
> something insane", and it is what a hand-drawn curve editor invites. It is
> not a defect when it sounds harsh. For reference, CUSTOM at its untouched default
> diagonal measures **THD −74.2 dB** — selecting it changes nothing until you draw.
>
> **Dragging on the curve selects CUSTOM for you**, the same way dragging on the
> oscillator selects DRAW: the curve you were looking at is copied into the 128 points
> first, so you deform it rather than jumping to a diagonal. DRIVE, MORPH and the old
> curve choice are untouched, so picking the previous curve puts it back.

> **The source window follows the SOURCE mode (v0.18).** It used to draw the loaded
> sample whatever the mode, so in OSC you were looking at a sample and hearing an
> oscillator. Now it shows the view that matches: the region editor for
> SAMPLE/CYCLE/TAPE, the one-cycle oscillator shape for OSC, and a short note for
> NOISE/INPUT (neither has a stored waveform). The window title says which you are
> looking at. The small display in the SOURCE block follows the same rule.
>
> **You draw the oscillator by hand, and drawing is how you turn drawing on (v0.19).**
> Open the source window in OSC mode and **just start dragging on the shape**. Whatever
> mode you were in, the cycle you can see is copied into the 128 editable points and OSC
> MODE flips to DRAW — so you deform the shape you were looking at (drag on a saw, you
> dent a saw). Your OSC WAVE choice and the 64 harmonic amplitudes are untouched, so
> pressing **WAVE** puts the previous sound back exactly. Nothing is lost by trying it.
>
> It is a pencil across **128 points**, not handles, and the dots show where those points
> sit. Sharp corners buzz and alias; that is the era, not a defect — the point count
> changes how smooth a *curve* can be, never how much grit an edge has. **SINE** resets
> the shape. **FROM SAMPLE** is the classic "convert sample to oscillator" trick: it fills the
> 128 points from the current CYCLE window of the loaded sample so you have something real
> to deform. All of it writes ordinary parameters, so it is saved in presets and
> automatable.
>
> **OSC MODE lives in three places now**, because in v0.18 it lived only in the EDIT
> view's HARMONICS block and nobody could find it: the WAVE/HARMONIC/DRAW strip at the top
> of the source window, an OSC MODE combo in the SOURCE block beside OSC WAVE, and the
> original EDIT-view combo (relabelled from "MODE").
>
> **One row of the SOURCE block swaps contents instead of greying out.** OSC WAVE + OSC
> MODE and AMP NZ + PH NZ share the row under the PITCH controls, and no mode uses both,
> so the pair that does not apply is hidden rather than dimmed. Everywhere else on the
> panel, inactive controls still grey out as usual.

> **The sample editor opens in its own window (v0.17).** It used to cover the whole
> panel, which meant you could not see the knobs you were reaching for. It is now a
> separate resizable window you can park beside the plugin, so region edits and the
> MANGLE controls are visible at once. Close it with its CLOSE button, Escape, or the
> window's own close box; reopening restores your zoom and scroll position.
>
> **Edits are heard immediately, even on a finished one-shot.** With LOOP off and PLAY
> latched, the sound used to play once and go silent, so nothing you dragged was
> audible until you pressed STOP and PLAY again. Moving the region, flipping LOOP/REV,
> or changing XFADE now re-arms the sound in place, with no re-attack click.

> **Factory preset tuning is deliberate, not sloppy.** The *defaults* are C-rooted
> (DSP-NOTES §0), but factory presets are free to sit off any note — `40-spectral-ghost`
> rings at 400 Hz (G4 +35 cents), `10-death-vocal` modulates at 40 Hz (D#1 +49 cents),
> and several others are similar. That detuning is character, chosen by ear, and it is
> **not** to be "corrected" in a future tuning audit. Anyone who wants a preset in tune
> with their material has the two tuners and the RES/MOD/STRETCH knobs to move it there.
> (Decided 2026-08-28.)

> **A preset is a sound design, not a sample.** Loading one never touches your loaded
> sample or its file path, and the factory presets leave your region markers alone —
> so you can audition every preset against the same chunk of a field recording.
> Your own saved presets DO store the region, so re-loading one restores exactly what
> you had. The host's own program menu is deliberately left with a single entry: hosts
> restore program 0 on project load, which would silently overwrite your saved state.


## What each SOURCE mode needs

| Mode | Material | MIDI note? | Its own controls | Notes |
|---|---|---|---|---|
| **Sample** | a loaded file | yes | region editor, LOOP/STYLE/REV, XFADE, STRETCH, PITCH MIX | Root note C3: play C3 for original speed; lower = slower and longer. |
| **Cycle** | a loaded file | yes | POS + LEN (window), CYCLE XFADE (EDIT) | Loops a tiny window at note pitch — the "convert sample to oscillator" trick. Try a very low note. |
| **Osc** | none | yes | OSC WAVE; Harmonic mode + 64 bars (EDIT) | sine / tri / saw / square / bell / odd. |
| **Noise** | none | yes | AMP NZ + PH NZ | The era model: a randomized sine. Both at 0 = a plain tunable sine; both at 100 = white noise. |
| **Input** | live audio on the track | **no** | IN TRIM | The only mode that sounds without playing a key — it free-runs, amp envelope bypassed. PITCH works on it (tape varispeed). |
| **Tape** | a take: REC then FLIP | yes | same as Sample: region, LOOP/STYLE/REV, XFADE, STRETCH | Root C3. Before v0.14 this was a one-shot only. |

Everything downstream of SOURCE (modulator, shaper, filter, resonator, inverter, delay,
envelopes, colour) applies to every mode identically.

**You do not need a MIDI keyboard.** Every mode in that table marked "yes" under MIDI can
be triggered by the **PLAY** button instead — it latches a note at the root (C3), so both
hands stay free for the knobs while the sound runs. With LOOP off the sample still plays
once and stops on its own, exactly as if you were holding a key. PLAY is dimmed in Input
mode, which free-runs and needs no note at all.

## Broken FX panel (v0.35 update)

Broken FX's SOURCE is always Input (true from the start of the split), but v0.35 gives
every visible control something to do — see DSP-NOTES.md §2a/§14a for the mechanisms.

- **SOURCE column widens back to 300 px** (was narrowed to 200 for the old, IN-TRIM-only
  FX column) and grows the FX window by the same 100 px so MANGLE (620) and OUTPUT (544)
  keep their tuned widths. It now shows the **waveform display** (drop a sample, or click
  to browse; EDIT opens the region pop-out, same as the instrument) and the **WINDOW POS /
  LEN** knobs above the existing PITCH/FINE/TUNE/IN TRIM cluster, with the IN tuner still
  pinned at the bottom. A loaded sample never plays as the SOURCE (that stays Input) — it
  feeds **MOD SRC SAMPLE** and the waveshaper's **FROM SAMPLE**, both of which now work in
  FX. The empty-state message says so: "Drop a sample: MOD SRC SAMPLE modulates with it,
  FROM SAMPLE shapes the curve with it." Double-clicking or hitting EDIT opens the same
  region-editing pop-out as the instrument, minus LOOP/STYLE/REV/XFADE (Sample/Tape
  *source*-playback controls that do nothing once the SOURCE is fixed to Input) — only the
  region itself and ZOOM SEL/SNAP/CLEAR/FIT matter here, because the Sample mod source and
  FROM SAMPLE both key off the region, not the loop settings.
- **OSCILLATOR is back** (row 1 of the EDIT panes becomes OSCILLATOR | WAVESHAPER, two-up
  instead of the instrument's three-up with ENVELOPES, which stays hidden — no notes ever
  fire in an effect). It no longer feeds the SOURCE; it exists because **MOD SRC TABLE**
  (new, see below) plays back whatever shape is showing here. XFADE/PITCH MIX/LOOP XFADE
  are hidden (Sample/Cycle/Tape-source-only); PITCH EXT stays (it gates PITCH's range, and
  PITCH is live on Input via tape-head varispeed in both products).
- **MOD SRC gains a fifth option, Table:** plays back the OSCILLATOR panel's current shape
  — whichever of Wave/Harmonic/Draw is selected — as a looping wavetable at MOD FREQ. Lets
  FX carry its own oscillator content into the modulator without needing a note or a
  sample.
- **FM INDEX (MODULE TRIMS) now works.** FM on the live input was dead in both products
  before v0.35; it is now a real varispeed effect on the signal (DSP-NOTES §14a), and since
  FX's source is always Input, this is the first time FM does anything at all in FX.
- **TAPE is back**, stacked full-width above OUTPUT in the right column (PLAY stays
  hidden). REC/FLIP/SAVE/TAKE all work exactly as in the instrument — the tape records the
  FX chain's own stereo output (each channel into its own take) regardless of what the
  SOURCE is — but the TAPE lamp ("lit when TAPE is the active SOURCE") is hidden, since FX
  can never make TAPE the SOURCE. SAVE writes a 2-channel WAV when both channels' takes
  match in length (they record in lockstep), else falls back to the L channel alone.
- **TIME retitles to FLATTEN.** STRETCH and its AMOUNT/FREQ/PREDELAY knobs are hidden
  (sample-playback only); FLATTEN and RESP still work on the live input.
- **TRIMS drops the NOISE segment.** AMP NZ / PH NZ are Noise-source-only and hidden; FM /
  DELAY / RESONATOR / INVERT stretch to fill the row.
- **FROM SAMPLE** (WAVESHAPER) is no longer instrument-only: with a sample loaded, it fills
  the Custom transfer curve from the CYCLE window exactly as in the instrument.
