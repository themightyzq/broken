#pragma once
// Broken GUI: ONE industrial-dark panel showing everything at once (v0.22). It used to
// be a fixed 980x560 window with a MANGLE/EDIT toggle, so half the instrument was always
// hidden - you could not watch a filter envelope while turning the knob that feeds it.
//
// Everything now lays out at a fixed DESIGN size inside `content`, which is then SCALED to
// whatever size the user drags the window to. That keeps every existing layout calculation
// in design coordinates (nothing needed re-tuning) at the cost of a locked aspect ratio.
// See docs/PANEL.md for the control spec this implements.

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/BrokenLookAndFeel.h"
#include "ui/PresetBar.h"
#include "ui/MangleView.h"
#include "ui/EditView.h"

namespace broken
{
class BrokenEditor : public juce::AudioProcessorEditor
{
public:
    explicit BrokenEditor (BrokenProcessor&);
    ~BrokenEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // design coordinates: header + MANGLE + EDIT, everything visible together.
    // Matches ClaudeDesign/design_handoff_broken_ui/README.md's Layout section (1520 wide,
    // 18px side padding, 10px gaps, top row 300|620|1fr, bottom row 3 equal columns +
    // MODULE TRIMS/TIME) — see MangleView/EditView::resized() for the grid math.
    static constexpr int designW      = 1520;
    static constexpr int headerH      = 48;    // logo + stamp + preset bar
    static constexpr int mangleH      = 545;   // SOURCE column needs this much (top row)
    static constexpr int editH        = 415;   // ENVELOPES/OSCILLATOR/WAVESHAPER row + MODULE TRIMS/TIME row
    static constexpr int designH      = headerH + mangleH + editH + 16;

private:
    // Holds the whole panel at the design size; the transform on THIS is what scales.
    struct Content : juce::Component
    {
        void paint (juce::Graphics& g) override;
        void resized() override;
        BrokenEditor* owner = nullptr;
    };

    BrokenProcessor& proc;
    ui::BrokenLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow;

    Content content;
    // Grime: noise + vignette + corner blotches + slot-head screws, baked once per size
    // into an image and composited last. Purely cosmetic; never intercepts the mouse.
    struct Grime : juce::Component
    {
        Grime() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override;
        void resized() override { cache = {}; }
        juce::Image cache;
    };
    Grime grime;
    // "BROKEN" with the K drawn mirrored (the user's nod to the album lettering). Font is
    // the panel's system bold for now; a custom face is the user's to supply.
    struct Logo : juce::Component, juce::SettableTooltipClient
    {
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (onClick && e.mouseWasClicked()) onClick();
        }
        std::function<void()> onClick; // opens the About overlay (v0.34)
    };
    Logo title;

    // About overlay (v0.34): version, licence, credits. Click/Escape dismisses.
    struct AboutOverlay : juce::Component
    {
        AboutOverlay() { setWantsKeyboardFocus (true); }
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override { setVisible (false); }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::escapeKey) { setVisible (false); return true; }
            return false;
        }
        void visibilityChanged() override { if (isVisible()) grabKeyboardFocus(); }
    };
    AboutOverlay about;
    ui::PresetBar presetBar;

    ui::MangleView mangleView;
    ui::EditView editView;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrokenEditor)
};
} // namespace broken
