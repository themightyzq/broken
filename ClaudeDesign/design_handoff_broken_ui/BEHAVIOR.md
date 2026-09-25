# BROKEN — what every UI element does

## Layout
- One window: MANGLE (the performance face) across the top, twelve EDIT blocks in a 4 × 3 grid beneath. No view switching.
  (Note: in the approved redesign the twelve EDIT blocks are regrouped into five panes — ENVELOPES, OSCILLATOR, WAVESHAPER, MODULE TRIMS, TIME — and VOICE lives inside PLAY. Same controls, same behavior; see README Layout.)
- A separate pop-out "source window" opens from the waveform display and shows whatever the current source is (sample region editor, oscillator shape, or a note for Noise/Input).

## Header
- BROKEN logo — the K is drawn backwards.
- < and > — step to the previous / next preset.
- PRESET dropdown — pick a factory or user preset. Shows a * when the sound has changed since loading.
- SAVE — save the current sound as your own preset.
- … — open your preset folder in Finder.

## SOURCE block
- Source dropdown — what gets mangled: Sample, Cycle (a tiny slice of the sample looped as an oscillator), Osc, Noise, Input (live audio), or Tape (the last resampled take).
- Waveform display — shows the loaded sample with the selected region tinted, or the oscillator's shape in Osc mode. Drop a file on it to load; click to browse when empty; double-click to open the source window. In Cycle mode it also outlines the oscillator window.
- EDIT — opens the source window.
- PLAY / STOP — plays the sound at the root note with no MIDI keyboard; click again to stop. Not available in Input mode.
- PITCH — transpose. On a sample this also changes speed, by design. ±24 semitones, ±48 with PITCH EXT on.
- FINE — fine tune, ±50 cents.
- TUNE — one press locks the source to the nearest note, using the IN tuner. Only lights when the tuner has a confident pitch.
- POS — where in the sample the Cycle oscillator window starts. Cycle mode only.
- LEN — how much of the sample becomes the Cycle oscillator. Cycle mode only.
- IN TRIM — level of the live input into the chain. Input mode only.
- OSC WAVE — the oscillator's waveform (sine, tri, saw, square, bell, odd). Only shown in Osc mode.
- OSC MODE — Wave (preset shape), Harmonic (built from 64 partials), or Draw (hand-drawn). Only shown in Osc mode.
- AMP NZ — how random the noise source's amplitude is. Only shown in Noise mode, in the same slot as OSC WAVE.
- PH NZ — how random the noise source's phase is. Only shown in Noise mode.
- IN tuner — shows the pitch coming in, before any processing: note name, cents needle, Hz.

## MANGLE block
- On/off lights above DRIVE, MOD, FILTER and RES, and beside INVERT and DELAY — hard-bypass that module.
- DRIVE — how hard the sound hits the waveshaper curve.
- CURVE — which waveshaper curve: Linear (clean), Hard Clip, Soft Sat, Fold, Asym, Stair, Sine, Invert-S, Random, or Custom (drawn in the EDIT view).
- MORPH — blend between the clean sound and the shaped one.
- MOD — how much the modulator affects the sound. The "death vocal" control.
- MODE — how it modulates: AM, RM, FM or PM.
- WAVE — the modulator's waveform: sine, bell or odd.
- SRC — what does the modulating: the internal oscillator, the sound itself, the sample, or the tape.
- FREQ — the modulator's frequency.
- FILTER — low-pass cutoff. Only a low-pass, by design. The knob stops at 500 Hz unless FLOOR EXT is on.
- POLES — filter steepness: 1 to 4 poles (6 to 24 dB per octave).
- RES — the resonator's pitch (a ringing comb).
- FB — how long the resonator rings. Negative values sound hollower.
- INVERT — how much of the spectrally flipped signal is mixed in.
- DELAY — delay time.
- MIX — how much delay is mixed in.
- INV — flips the polarity of the delayed signal.

## PLAY block
- MONO / POLY — one voice with last-note priority, or six playable voices.
- UNISON — stacks all six voices, detuned, on one note.
- SPREAD — how far apart the unison voices are detuned.
- A / D / S / R — the amp envelope: attack, decay, sustain, release.

## TAPE block
- REC — records the plugin's output into the tape (up to 10 s). Blinks while actually recording.
- FLIP — makes the recording the new source, so it can be mangled again.
- SAVE — saves the current take as a 24-bit WAV. Only available once a take exists.
- TAPE light — lit when the tape is the active source.

## OUTPUT block
- Level meter — output level; changes colour above −6 dB.
- COLOUR — emulates the bit depth of the sampler this would have been rendered to: off, 12-bit or 8-bit.
- RATE — sample-rate reduction, with no smoothing on purpose.
- OUT — master level.
- OUT tuner — shows the pitch coming out after everything.

## EDIT blocks
- FILTER ENV: A / D / S / R — the filter envelope. ENV AMT — how far it moves the cutoff, up to ±60 semitones.
- AUX ENV: A / D / S / R — a third envelope. DEST — what it modulates: filter cutoff, resonator pitch, invert mix, mod amount, or pitch. AMT — how much, positive or negative.
- CYCLE / OSC: XFADE — smooths the seam of the Cycle loop. PITCH MIX — blends pitched playback with the unpitched original (the classic chorus trick). OSC WAVE — same control as in the SOURCE block. PITCH EXT — extends PITCH from ±24 to ±48 semitones.
- FM: FM INDEX — how deep the FM goes when MODE is FM.
- DELAY: FINE — fine trim of the delay time. FEEDBACK — how many repeats.
- RESONATOR: DAMP — rolls off the resonator's high frequencies.
- CURVE: the waveshaper's transfer curve, drawn live. Drag on it to draw your own — that switches CURVE to Custom automatically and keeps the shape you were looking at. TRIM — output level after the shaper. RND — rolls a new random curve. COPY TO CUSTOM — copies the selected curve into the drawable one. FROM SAMPLE — uses a slice of the loaded sample as the curve itself.
- VOICE: RETRIGGER — restart the amp envelope on legato notes. BEND — pitch-wheel range in semitones; 0 switches the wheel off.
- FILTER: FLOOR EXT — lets the filter go below the default 500 Hz floor.
- HARMONICS: 64 draggable bars, one per partial, for Harmonic mode; Shift-drag draws a straight line across them. OSC MODE — same control as in the SOURCE block. SAW / SQR / FLAT — preset partial recipes.
- TIME: STRETCH — time-stretch by repeating segments. AMOUNT — positive stretches, negative compresses. FREQ — segment size; match it to the sound's fundamental. PREDELAY — leaves the attack untouched before stretching starts. FLATTEN — levels out the sound's own dynamics, like heavy compression. RESP — how fast FLATTEN reacts.
- COLOUR / NOISE: AMP NZ / PH NZ — same as the SOURCE-block pair. INV TYPE — which spectral-inversion flavour, A or B. XFADE SHAPE — linear or equal-power loop crossfade.

## Source window (pop-out)
- Follows the source: Sample, Cycle and Tape show the region editor; Osc shows the oscillator shape; Noise and Input show a short note saying there is nothing to edit.

### Region editor (Sample / Cycle / Tape)
- File name and a readout of the selection's start, end and length. The name turns red if the file is missing.
- Waveform — drag to select the region that plays; scroll to zoom; drag the background to pan. Shows the playhead while playing and the loop crossfade.
- Minimap — overview of the whole file; drag the box to move the view.
- LOOP — off plays the region once; on repeats it while the key is held.
- STYLE — loop back to the start, or ping-pong. Only active when LOOP is on.
- REV — plays the source backwards. Works whether or not it loops.
- XFADE — smooths the loop seam or the ping-pong turnaround, in milliseconds. Only active when LOOP is on.
- ZOOM SEL — zooms to the current selection.
- SNAP — snaps selection edges to zero crossings.
- CLEAR — resets the region to the whole file.
- FIT — zooms back out to the whole file.
- CLOSE — closes the window.

### Oscillator editor (Osc)
- WAVE / HARMONIC / DRAW — choose the oscillator mode.
- Shape display — one cycle of what the oscillator is playing. Drag on it to draw: that switches to Draw automatically and starts from the shape on screen, so nothing is lost.
- FROM SAMPLE — grabs a slice of the loaded sample as the drawn shape.
- SINE — resets the drawn shape to a sine.
- Hint text — tells you what a drag will do in the current mode.

## Behaviour to keep
- Controls that don't apply to the current source mode are greyed out, except the OSC WAVE/OSC MODE and AMP NZ/PH NZ pair, which swap places in the same slot.
- Drawing on the curve or the oscillator shape switches into the drawable mode by itself; the previous choice is untouched, so switching back restores the old sound.
- The same control appears in more than one place: OSC MODE three times, OSC WAVE twice, the two noise knobs twice. They always show the same value.
- PLAY needs no MIDI keyboard; it plays the root note (C3).
