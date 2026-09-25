#pragma once
// Enlarges a small control's CLICKABLE area to the house accessibility floor -- >=22 px on
// screen at the editor's 0.65x resize floor, i.e. >=34 px in DESIGN coordinates
// (ceil(22 / 0.65) = 33.85; see the house accessibility floor and PluginEditor.cpp's resize-floor
// comment) -- WITHOUT changing how big the control looks.
//
// `hitTest()` cannot do this: overriding it only reshapes what counts as "inside" THIS
// component, but the control's own BOUNDS are still what a click has to land in, and
// those bounds are exactly what's too small. This instead gives the control a bigger
// bounds indirectly: HitPad owns the real (>=34px) bounds, and the control sits centred
// inside it at its own unchanged size.
//
// Companion to MangleView.h's `lightRowH` technique, which gets the same result a
// different way: LitToggle already draws its LED at a fixed small diameter regardless of
// the bounds it's given, so simply enlarging LitToggle's OWN bounds is enough (the extra
// room around the LED is invisible padding already, by construction). TextToggle,
// TextButton and ComboBox are not built that way -- their LookAndFeel methods
// (drawButtonBackground/drawButtonText/drawComboBox) paint the ENTIRE bounds they're
// given as the visible control, so enlarging THEIR bounds directly would visibly enlarge
// them too. HitPad wraps them instead: the control keeps its existing small bounds, and a
// separate, bigger, invisible HitPad around it catches clicks that land in the margin.
//
// Usage: keep the wrapped control (TextButton, the shared TextToggle/Combo, etc.) as a
// normal member, declared BEFORE the HitPad that wraps it (member init order); add the
// HitPad to the parent view instead of the control itself, and lay it out with
// setPadded(): the padBounds rectangle is the real hit area (make its short dimension
// >=34 design px), innerW/innerH is the size the control keeps painting at, centred
// inside padBounds.

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace broken::ui
{
class HitPad : public juce::Component
{
public:
    // `innerIn` is not owned here -- the caller keeps it alive (as a plain member, or via
    // its own unique_ptr) for at least as long as this HitPad. `onPadClickIn` fires the
    // same action a direct click on `innerIn` would (e.g. `button.triggerClick()` or
    // `combo.showPopup()`).
    HitPad (juce::Component& innerIn, std::function<void()> onPadClickIn)
        : inner (innerIn), onPadClick (std::move (onPadClickIn))
    {
        addAndMakeVisible (inner);
    }

    // padBounds becomes this component's own bounds (the real, floor-sized hit area);
    // `inner` is centred inside it at innerW x innerH, its unchanged visual size.
    void setPadded (juce::Rectangle<int> padBounds, int innerW, int innerH)
    {
        setBounds (padBounds);
        inner.setBounds (getLocalBounds().withSizeKeepingCentre (innerW, innerH));
    }

    // Only ever reached for a click that STARTS in the padding: JUCE routes the whole
    // mouseDown/drag/up gesture to whichever component was hit at mouseDown time, so a
    // click starting on `inner` itself is handled by `inner` directly and never reaches
    // this component's mouseUp at all -- no double-firing.
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onPadClick && e.mouseWasClicked())
            onPadClick();
    }

private:
    juce::Component& inner;
    std::function<void()> onPadClick;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitPad)
};
} // namespace broken::ui
