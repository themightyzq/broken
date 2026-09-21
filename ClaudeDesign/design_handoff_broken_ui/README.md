# Handoff: BROKEN — Plugin UI Redesign

## Overview
A full visual redesign of the BROKEN audio mangler plugin (JUCE). Direction: degraded 90s rackmount sampler — black rack-metal faceplate, teal silkscreen labels, green vacuum-fluorescent/LCD readouts, black scalloped rubber knobs with printed tick scales, orange status LEDs, and a film-grain/vignette "grime" layer over everything. Single window, no view switching.

## About the Design Files
The files in this bundle are **design references created in HTML** — prototypes showing intended look and behavior, not production code. The task is to **recreate this design in the existing JUCE codebase** (custom `LookAndFeel` + `Component` hierarchy), using its established parameter/attachment patterns. Do not port the HTML.

- `Broken 5 Rack.dc.html` — the approved design (open in a browser to inspect; all styles are inline).
- `screenshots/full-ui.png` — full-window reference render.
- `BEHAVIOR.md` — the complete functional spec for every control (source of truth for behavior).
- `previous-iteration/` — earlier direction, for context only.

## Fidelity
**High-fidelity.** Colors, typography, spacing, and proportions are final. Recreate pixel-perfectly at 1520 × ~1120 logical px (design should scale uniformly with plugin window resize).

## Design Tokens

### Palette
| Token | Value | Use |
|---|---|---|
| chassis-bg | vertical gradient `#0E1011 → #0A0B0C (60%) → #0D0F10` | window background |
| panel-face | vertical gradient `#1D2022 → #121416` | all panels |
| panel-border | `#060707` outer 1px; inset ring `rgba(0,0,0,.6)`; top inner highlight `rgba(255,255,255,.04)`; drop `0 2px 8px rgba(0,0,0,.5)` | panel chrome |
| silk-title | `#66B7AE` | section titles |
| silk-label | `#8FB3AE` | knob/control labels |
| silk-caption | `#62807C` | sub-captions, disabled text |
| rule | `#22272A` (panel title rules), `#1B1F21` (inner dividers) | hairlines |
| lcd-bg | `#0C150E`, border `#1F2B21`, inset shadow `0 2px 6px rgba(0,0,0,.7)` | all readouts/dropdowns/displays |
| lcd-text | `#8FE89A` + glow `0 0 7px rgba(90,220,110,.5)` | primary LCD values |
| lcd-dim | `#63B871` | LCD secondary (PRESET, carets, TAKE) |
| lcd-faint | `#4E8A5A` / `#3F7A4A` | LCD hints, scale numerals, needles |
| lcd-screen-dark | `#0A120C` | waveform stripes, meter well, tuner bar |
| btn-face | gradient `#212326 → #141618`, border `#0A0B0C`, shadow `0 2px 0 rgba(0,0,0,.6)` + inset top highlight `rgba(255,255,255,.07)` | buttons |
| btn-text | `#C9D4D2` (primary) / `#B9C7C4` (secondary) | button legends |
| btn-disabled | gradient `#17191B → #101214`, text `#62807C`, no drop shadow | TUNE, TAPE SAVE |
| accent | `#E8622A` | LEDs, active toggles (MONO, RETRIG), hover text |
| pointer | `#DED6C2` | knob pointers |
| tick | `rgba(122,186,178,.45)` | knob tick rings, meter scale |
| meter-fill | gradient `#2F8F45 → #6FD57E (70%) → #8FE89A` | output meter |
| logo/text-bright | `#E2E5E8` | logo |
| footer | `#45524F` | serial/footer stamps |

### Typography
- **Barlow Condensed** (Google Fonts) — all silkscreen text. Titles: 600, 13px, letter-spacing 3.5px, uppercase. Labels: 600, 10–12px, ls 1.6–2px. Logo: 600, 26px, ls 8px.
- **VT323** (Google Fonts) — every LCD value: 14–18px depending on field.
- **IBM Plex Mono** — serial stamps only (9–10px).
- All text has `text-shadow: 0 1px 0 rgba(0,0,0,.9)` (silkscreen) or the green glow (LCD).

### Knobs (custom `LookAndFeel::drawRotarySlider`)
Black scalloped rubber, four sizes: **60px** (MANGLE primary), **42px**, **38px**, **34px**.
1. Scallop ring: 12 teeth — repeating 30° conic segments, `#2E2E2E` tooth / `#070707` gap.
2. Cap: radial gradient offset to 35%/28%: `#505050 → #242424 (56%) → #080808 (66%)`, covering ~68% of radius.
3. Wear (60/44px knobs only): 2–3 darker conic wedges over the ring (`rgba(0,0,0,.5)` ≈ 10° wide) + one faint light wedge — chipped edges.
4. Border 2px `#040404`; inset shadow top.
5. Pointer: 3px × ~30% radius bar, `#DED6C2`, from top edge, rotates −135°…+135°.
6. Tick ring printed on the panel around the knob (not on it): radial ticks every 30°, color `tick`, ring sits ~6–8px outside the knob edge.

### LCD fields
Height 22–26px, no rounding, VT323 value left, `▾` caret right in lcd-dim. Same treatment for value readouts under knobs (0 7px padding, min-width to fit).

### LEDs
Round, 9–10px, accent fill, 1px `rgba(0,0,0,.7)` rim, glow `0 0 9px accent`. Unlit: `#151719` fill, `#2A2F31` rim. REC button carries a small dark-red dot (`#5A2018`) that lights/blinks red while recording.

### Grime layer (paint over the whole window, non-interactive)
1. Monochrome noise (feTurbulence-style, ~240px tile), blend **overlay**, opacity `0.10 + grime×0.50` (default grime 0.5 → 0.35).
2. Vignette: radial darkening to `rgba(0,0,0,.55)` at edges + two corner blotches, opacity `0.3 + grime×0.5`.
3. Optional scanlines: 1px black lines every 3px, opacity 0.16 (off by default).
4. Four slot-head screws in window corners (12px, radial steel, random slot angles).

## Layout (1520px wide, 18px side padding, 10px gaps)

### Header (~48px)
Logo "BROKEN" with the **K mirrored horizontally** · "REV.C / SN 0113" stamp · spacer · `<` `>` preset step buttons (32×30) · PRESET LCD (500×30, shows `*` when dirty) · SAVE button · `…` button.

### Top row — grid `300px | 620px | 1fr`
- **SOURCE (300)**: source dropdown LCD; waveform display (118px tall, dark green stripes, "DROP SAMPLE or CLICK" hint, EDIT chip bottom-right); PLAY button (30px); PITCH (44px knob, readout `-0.0`) + FINE (readout `0 c`) + TUNE button (disabled state shown); POS / LEN / IN TRIM row (36px knobs, 30% opacity when inapplicable); IN tuner LCD box pinned to panel bottom (note `––`, cents bar with center needle, `–– Hz`).
- **MANGLE (620)**: rows share one 4-column grid so knobs align vertically. Row 1: DRIVE / MOD / FILTER / RES — 60px knobs, LED above each, LCD readout below (`12.0`, `0.0`, `20000.0`, `130.8`). Row 2: dropdowns CURVE (150px) / MODE / WAVE / SRC (72px each) / POLES (104px). Hairline. Row 3: MORPH / FREQ / FB (42px) in columns 1–3. Row 4: INVERT / DELAY (LEDs) / MIX (42px) in columns 1–3, INV button column 4.
- **Right column**: PLAY and TAPE side-by-side, OUTPUT spanning below.
  - **PLAY**: MONO/POLY segmented toggle (MONO active = accent fill, dark text); UNISON button + SPREAD (34px); A D S R row (34px); divider; RETRIG toggle (active) + BEND (34px).
  - **TAPE**: REC (with dot LED), FLIP, SAVE (disabled until a take exists); TAKE LCD (`TAKE --` / `0.0s`); TAPE LED + label at bottom.
  - **OUTPUT** (horizontal): COLOUR dropdown (130px) · RATE · OUT (42px knobs) · OUT tuner LCD (flex) · vertical meter (13px wide, full height, tick scale printed left, fill from bottom, colour shifts above −6 dB).

### Bottom row — grid `repeat(3, 1fr)`
- **ENVELOPES**: two rows on a shared grid `30px | 5 knob cols | 100px` → columns align. Row FLT: A D S R AMT (38px) + FLOOR EXT button. Hairline. Row AUX: A D S R AMT + DEST dropdown (FilterCut).
- **OSCILLATOR**: harmonics display (80px tall, 64 green bars, "1"/"64" numerals) + right stack (132px): OSC MODE dropdown, OSC WAVE dropdown, SAW/SQR/FLAT button row. Below: XFADE + PITCH MIX (38px), PITCH EXT button, LOOP XFADE dropdown (Linear).
- **WAVESHAPER**: transfer-curve display (150×104, green S-curve + faint ghost stroke) with FROM SAMPLE button beneath; right: TRIM (38px) + RND button, COPY TO CUSTOM button.
- **MODULE TRIMS** (spans 2 columns): five segments with 1px dividers — FM (FM INDEX) · DELAY (FINE, FEEDBACK) · RESONATOR (DAMP) · INVERT (TYPE dropdown) · NOISE (AMP NZ, PH NZ). Segment captions in silk-caption.
- **TIME**: STRETCH + FLATTEN buttons stacked left; AMOUNT / FREQ / PREDELAY / RESP (34px) right.

### Footer
`BRKN-01 · DO NOT SERVICE · 48V PHANTOM PAIN` left, `MFD 2026 · CALIBRATED NEVER` right (IBM Plex Mono 9px, footer color).

## Interactions & Behavior
`BEHAVIOR.md` in this bundle is the complete, authoritative control-by-control spec (source modes, conditional visibility, the shared-slot OSC WAVE/AMP NZ swap, draw-to-switch behavior, tuners, tape workflow, source pop-out window). Key visual rules:
- Inapplicable controls render at 30% opacity (greyed), except the OSC WAVE/OSC MODE ↔ AMP NZ/PH NZ pairs, which swap places in the same slot.
- Hover on any button/chip: legend turns accent `#E8622A` (~100ms ease).
- Active toggles (MONO, RETRIG, UNISON when on): accent fill, `#140D07` text, inset bottom shadow `rgba(0,0,0,.35)`.
- LEDs above DRIVE/MOD/FILTER/RES and beside INVERT/DELAY are bypass toggles: lit accent = active, unlit = hard-bypassed.
- Level meter turns from green gradient toward amber/red above −6 dB.
- The source pop-out window (region/oscillator editor) should reuse this same visual system: panel-face chrome, LCD displays, silkscreen labels.

## State / Parameters
No new parameters — this is a reskin of the existing parameter set described in BEHAVIOR.md. All conditional show/hide/grey logic keys off the SOURCE mode and LOOP state exactly as currently implemented.

## JUCE implementation notes
- One custom `LookAndFeel` owns: rotary slider (scalloped knob + pointer), tick rings (draw in parent `paint()` so they sit on the panel), LCD field (ComboBox + Label variants), button (gradient + hover accent), LED component.
- Fonts: bundle Barlow Condensed, VT323, IBM Plex Mono (all OFL-licensed on Google Fonts) as BinaryData `Typeface`s.
- Grime: render noise/vignette once into a cached `Image` per window size and composite it last in the editor's `paint()`; scanlines optional flag. Knob wear wedges can be per-knob-seeded so no two knobs chip identically.
- The mirrored K: draw "BRO", then a horizontally-flipped "K" (`AffineTransform::scale(-1, 1)` about its centre), then "EN".
