// broken_ui_snapshot: render the editor headlessly to a PNG, or audit hit-target sizes.
//
//   broken_ui_snapshot <out.png> [scale] [width height]
//   (scale defaults to 1.0; width/height default to the editor's constructed size --
//   pass them to gate a render at, e.g., the resize-limit minimum: setSize() bypasses the
//   constrainer entirely, same as any direct setBounds() call, so this can render sizes a
//   user could never actually drag to.)
//
//   broken_ui_snapshot --hit-audit
//   Constructs the editor at the house 0.65x resize floor (BrokenEditor::designW/designH
//   * 0.65, exactly what the constrainer clamps to -- see PluginEditor.cpp), walks every
//   visible Button/ComboBox/Slider (and every HitPad -- see ui/HitPad.h) in the tree, and
//   prints any whose ON-SCREEN bounds (i.e. after the content scale transform, computed
//   via getLocalArea() so no real screen peer is needed) are under 22 px in width or
//   height (the house accessibility floor). Also audits the pop-out source
//   editor window's content (SourceEditorPanel: SampleEditor and, for the instrument,
//   OscEditor) at that window's own minimum size (SampleEditorWindow's setResizeLimits,
//   640x340) -- that window is NOT scaled by the main editor's transform, so its floor is
//   a plain 22 px on its own hardcoded pixel constants. Exit code is the number of
//   violations found across every section audited (0 = clean).
//
// The grime layer is seeded and nothing in the panel animates, so two renders of the same
// code are byte-identical. That makes this the regression gate for look-and-feel work:
// render before, change, render after, compare.

#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "plugin/ui/HitPad.h"
#include "plugin/ui/SourceEditorPanel.h"
#include <iostream>
#include <vector>

namespace
{
constexpr int kMinHitPx = 22; // house accessibility floor

struct HitViolation
{
    juce::String label;
    int w = 0, h = 0;
};

// Best-effort human-readable name for a violation: accessible title first (most of these
// controls call setTitle explicitly), then the component name, then a button's own text,
// then its RTTI type as a last resort.
juce::String describeComponent (juce::Component& c)
{
    if (auto t = c.getTitle(); t.isNotEmpty()) return t;
    if (auto n = c.getName(); n.isNotEmpty()) return n;
    if (auto* b = dynamic_cast<juce::Button*> (&c))
        if (b->getButtonText().isNotEmpty())
            return b->getButtonText();
    return typeid (c).name();
}

// Walks `node`'s children (recursively). `root` is the coordinate frame violations are
// measured in -- the plugin editor itself for the main panel (so getLocalArea() folds in
// the content scale transform), or the pop-out panel itself for the unscaled window.
void collectHitViolations (juce::Component& root, juce::Component& node, std::vector<HitViolation>& out)
{
    for (int i = 0; i < node.getNumChildComponents(); ++i)
    {
        auto* child = node.getChildComponent (i);
        if (child == nullptr || ! child->isVisible())
            continue;

        // HitPad IS the accessible hit target by design (see ui/HitPad.h): check its own
        // bounds and do not descend into the smaller control it deliberately pads around.
        if (auto* pad = dynamic_cast<broken::ui::HitPad*> (child))
        {
            auto onScreen = root.getLocalArea (pad, pad->getLocalBounds());
            if (onScreen.getWidth() < kMinHitPx || onScreen.getHeight() < kMinHitPx)
                out.push_back ({ describeComponent (*pad), onScreen.getWidth(), onScreen.getHeight() });
            continue;
        }

        const bool isControl = dynamic_cast<juce::Button*> (child) != nullptr
                             || dynamic_cast<juce::ComboBox*> (child) != nullptr
                             || dynamic_cast<juce::Slider*> (child) != nullptr;
        if (isControl)
        {
            auto onScreen = root.getLocalArea (child, child->getLocalBounds());
            if (onScreen.getWidth() < kMinHitPx || onScreen.getHeight() < kMinHitPx)
                out.push_back ({ describeComponent (*child), onScreen.getWidth(), onScreen.getHeight() });
        }

        // Recurse regardless: containers (Block, the SOURCE/MANGLE/etc. panels, plain
        // Components like WaveformDisplay) hold further controls, and a Button/ComboBox/
        // Slider is not expected to itself parent another independently-clickable control,
        // but recursing costs nothing if it does.
        collectHitViolations (root, *child, out);
    }
}

int printViolations (const juce::String& section, const std::vector<HitViolation>& v)
{
    if (v.empty())
    {
        std::cout << "[hit-audit] " << section << ": 0 violations (all >= " << kMinHitPx << "px)\n";
        return 0;
    }
    std::cout << "[hit-audit] " << section << ": " << (int) v.size() << " violation(s) under "
              << kMinHitPx << "px:\n";
    for (auto& item : v)
        std::cout << "    " << item.label << "  " << item.w << "x" << item.h << " px\n";
    return (int) v.size();
}

int runHitAudit()
{
    juce::ScopedJuceInitialiser_GUI gui;
    int totalViolations = 0;

    broken::BrokenProcessor processor;

    // ---- main scaled editor, at the 0.65x resize floor -----------------------------
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        if (editor == nullptr)
        {
            std::cerr << "createEditor returned null\n";
            return -1;
        }
        const int floorW = juce::roundToInt (broken::BrokenEditor::designW * 0.65);
        const int floorH = juce::roundToInt (broken::BrokenEditor::designH * 0.65);
        editor->setSize (floorW, floorH); // bypasses the constrainer, same as any setBounds()

        std::cout << "[hit-audit] main editor at 0.65x floor: " << floorW << "x" << floorH
                  << " (design " << broken::BrokenEditor::designW << "x"
                  << broken::BrokenEditor::designH << ")\n";

        std::vector<HitViolation> v;
        collectHitViolations (*editor, *editor, v);
        totalViolations += printViolations ("main editor (default / Sample source)", v);

#if !BROKEN_FX
        // MangleView's oscWaveCombo/oscModeCombo (SOURCE column) and noiseAmpKnob/
        // noisePhaseKnob only setVisible(true) in Osc/Noise source mode (MangleView.h
        // timerCallback()); the default Sample-mode pass above never walks them at all,
        // hidden components being skipped entirely, so audit again with the source
        // switched to Osc. BROKEN_FX forces the source to Input permanently and hides
        // sourceMode, so this mode switch is instrument-only.
        if (auto* p = processor.apvts.getParameter ("source.mode"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (2.0f)); // Osc
            p->endChangeGesture();
        }
        // MangleView's visibility swap happens on its own 10Hz Timer, not on parameter
        // change notification. This target builds with JUCE_MODAL_LOOPS_PERMITTED=0 (see
        // CMakeLists.txt), so runDispatchLoopUntil() isn't available -- sleep past the
        // 100ms timer period (real wall-clock time, no message loop needed) and then
        // process whatever timers are now due directly, which IS safe without one.
        juce::Thread::sleep (150);
        juce::Timer::callPendingTimersSynchronously();

        std::vector<HitViolation> vOsc;
        collectHitViolations (*editor, *editor, vOsc);
        totalViolations += printViolations ("main editor (Osc source)", vOsc);
#endif
    }

    // ---- pop-out source editor window content, at its own minimum size ------------
    // SampleEditorWindow (MangleView.h) sets setResizeLimits(640, 340, ...) and applies
    // NO scale transform to its content, unlike the main editor -- its controls are laid
    // out in real, hardcoded screen pixels at every window size, so the floor here is a
    // plain 22px against those pixel constants, not a design-coordinate conversion.
    //
    // A FRESH processor, deliberately: the main-editor pass above (instrument only) left
    // source.mode on Osc so it could reach MangleView's mode-gated combos, and reusing
    // that same processor here would silently default this panel into OscEditor's view
    // too instead of SampleEditor's -- caught by hand-verifying the "before" vs "after"
    // pop-out counts didn't match what SampleEditor.h's code implied they should.
    {
        broken::BrokenProcessor sampleProcessor;
        broken::ui::SourceEditorPanel panel (sampleProcessor);
        panel.setSize (640, 340);

        std::vector<HitViolation> v;
        collectHitViolations (panel, panel, v);
        totalViolations += printViolations ("pop-out window (Sample/Cycle/Tape view)", v);

#if !BROKEN_FX
        // Osc mode swaps in OscEditor instead of SampleEditor (see SourceEditorPanel.h);
        // BROKEN_FX forces isSampleish permanently true there, so this branch is
        // instrument-only -- Broken FX never shows OscEditor in the pop-out.
        if (auto* p = sampleProcessor.apvts.getParameter ("source.mode"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (2.0f)); // Osc
            p->endChangeGesture();
        }
        panel.show(); // forces immediate re-evaluation instead of waiting on its 8Hz timer
        panel.resized();

        std::vector<HitViolation> v2;
        collectHitViolations (panel, panel, v2);
        totalViolations += printViolations ("pop-out window (Osc view)", v2);
#endif
    }

    std::cout << "[hit-audit] TOTAL: " << totalViolations << " violation(s)\n";
    return totalViolations;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: broken_ui_snapshot <out.png> [scale] [width height]\n"
                     "       broken_ui_snapshot --hit-audit\n";
        return 2;
    }

    if (juce::String (argv[1]) == "--hit-audit")
    {
        const int violations = runHitAudit();
        return violations < 0 ? 1 : violations; // exit code doubles as the violation count
    }

    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;

    broken::BrokenProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::cerr << "createEditor returned null\n";
        return 1;
    }

    if (argc > 4)
    {
        const int width  = juce::String (argv[3]).getIntValue();
        const int height = juce::String (argv[4]).getIntValue();
        if (width <= 0 || height <= 0)
        {
            std::cerr << "width and height must be positive\n";
            return 2;
        }
        editor->setSize (width, height); // bypasses the constrainer, as documented above
    }
    else if (argc == 4)
    {
        std::cerr << "usage: broken_ui_snapshot <out.png> [scale] [width height]\n";
        return 2;
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::cerr << "could not write " << out.getFullPathName() << "\n";
        return 1;
    }

    std::cout << out.getFullPathName() << "  " << image.getWidth() << "x" << image.getHeight() << "\n";
    return 0;
}
