#pragma once
// Header-bar preset browser (docs/PANEL.md "Preset bar"): < > step, a menu of factory
// and user presets, SAVE to the user folder, and "..." to reveal that folder.
// A preset is a sound design, not a sample — loading one never touches the loaded file.

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "HitPad.h"
#include "../PluginProcessor.h"

namespace broken::ui
{
class PresetBar : public juce::Component,
                  private juce::Timer,
                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PresetBar (BrokenProcessor& p) : processor (p)
    {
        // `style` sets text/tooltip/onClick and parents `b` directly under the bar --
        // used for the buttons that are already >=22px on screen at the resize floor.
        // `styleNoAdd` does the same WITHOUT parenting `b`, for the three that need
        // padding instead (their own bounds stay too narrow on screen; see the HitPads
        // below and HitPad.h): the caller adds the pad, and HitPad's own constructor
        // parents `b` under the pad.
        auto styleNoAdd = [] (juce::TextButton& b, const juce::String& text,
                              const juce::String& tip, std::function<void()> action)
        {
            b.setButtonText (text);
            b.setTooltip (tip);
            b.onClick = std::move (action);
        };
        auto style = [this, &styleNoAdd] (juce::TextButton& b, const juce::String& text,
                             const juce::String& tip, std::function<void()> action)
        {
            styleNoAdd (b, text, tip, std::move (action));
            addAndMakeVisible (b);
        };

        styleNoAdd (prevButton, "<", "Step to the previous preset.",
               [this] { stepPreset (-1); });
        addAndMakeVisible (prevButtonPad);
        styleNoAdd (nextButton, ">", "Step to the next preset.",
               [this] { stepPreset (1); });
        addAndMakeVisible (nextButtonPad);
        style (saveButton, "SAVE",
               "Saves the current sound to your own preset folder.",
               [this] { promptSave(); });
        style (rndButton, "RND",
               "Rolls the dice: randomizes the sound. Your sample, source mode, output "
               "level, tape and hand-drawn curves are left alone, and feedback/drive are "
               "capped so it can never scream. UNDO takes you back.",
               [this] { processor.randomizeParams(); undoButton.setEnabled (true); });
        style (undoButton, "UNDO", "Restores the exact state from before the last RND.",
               [this] { processor.undoRandomize(); });
        undoButton.setEnabled (processor.hasRandomUndo());
        // screen-reader names: the panel has a second SAVE (tape) and RND (curve)
        saveButton.setTitle ("SAVE PRESET");
        rndButton.setTitle ("RANDOMIZE");
        styleNoAdd (revealButton, "...", "Preset actions: reveal folder, overwrite, rename, delete.",
               [this] { showOverflowMenu(); });
        addAndMakeVisible (revealButtonPad);

        presetBox.setTooltip ("Factory presets are built into the plugin; your own saved "
                              "ones appear under USER. A * means you've changed something "
                              "since loading.");
        presetBox.setTextWhenNothingSelected ("PRESET");
        presetBox.onChange = [this]
        {
            if (ignoreBoxChange) return;
            const int id = presetBox.getSelectedId();
            if (id > 0) applyPreset (id - 1);
        };
        addAndMakeVisible (presetBox);

        rebuildMenu();
        // any parameter edit marks the preset dirty
        for (auto* param : processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
                processor.apvts.addParameterListener (rp->paramID, this);
        startTimerHz (4);
    }

    ~PresetBar() override
    {
        for (auto* param : processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
                processor.apvts.removeParameterListener (rp->paramID, this);
    }

    void resized() override
    {
        // prevButton/nextButton/revealButton pad from their native 22/22/26px width to
        // the house floor (>=34px on screen at the resize minimum -- the bar's own height
        // is already a comfortable 34 design px here, so only width was short); see
        // HitPad.h. Their own drawn size (second setPadded argument) is unchanged.
        auto r = getLocalBounds().reduced (2, 3);
        auto prevArea = r.removeFromLeft (34);
        prevButtonPad.setPadded (prevArea, 22, prevArea.getHeight());
        r.removeFromLeft (2);
        auto nextArea = r.removeFromLeft (34);
        nextButtonPad.setPadded (nextArea, 22, nextArea.getHeight());
        r.removeFromLeft (4);
        auto revealArea = r.removeFromRight (34);
        revealButtonPad.setPadded (revealArea, 26, revealArea.getHeight());
        r.removeFromRight (3);
        undoButton.setBounds (r.removeFromRight (52));
        r.removeFromRight (3);
        rndButton.setBounds (r.removeFromRight (42));
        r.removeFromRight (6);
        saveButton.setBounds (r.removeFromRight (48));
        r.removeFromRight (4);
        presetBox.setBounds (r);
    }

private:
    void rebuildMenu()
    {
        const juce::ScopedValueSetter<bool> svs (ignoreBoxChange, true);
        presetBox.clear (juce::dontSendNotification);
        bool userHeaderAdded = false;
        const auto& entries = processor.presets.getEntries();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].isUser && ! userHeaderAdded)
            {
                presetBox.addSeparator();
                presetBox.addSectionHeading ("USER");
                userHeaderAdded = true;
            }
            presetBox.addItem (entries[i].name, (int) i + 1);
        }
        showCurrent();
    }

    void applyPreset (int index)
    {
        juce::String err;
        if (! processor.presets.load (index, err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Preset", err);
        dirtyFlag.store (false); // the load itself fired the listener for every id
        dirty = false;
        showCurrent();
    }

    void stepPreset (int delta)
    {
        juce::String err;
        processor.presets.step (delta, err);
        dirtyFlag.store (false);
        dirty = false;
        showCurrent();
    }

    void showCurrent()
    {
        const juce::ScopedValueSetter<bool> svs (ignoreBoxChange, true);
        const int idx = processor.presets.getCurrentIndex();
        if (idx >= 0)
        {
            presetBox.setSelectedId (idx + 1, juce::dontSendNotification);
            const auto name = processor.presets.getEntries()[(size_t) idx].name;
            presetBox.setText (dirty ? name + " *" : name, juce::dontSendNotification);
        }
    }

    void promptSave()
    {
        auto* w = new juce::AlertWindow ("Save preset", "Name your preset:",
                                         juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", suggestedName(), juce::String());
        w->addButton ("Save", 1);
        w->addButton ("Cancel", 0);
        juce::Component::SafePointer<PresetBar> safeThis (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [safeThis, w] (int result)
            {
                const auto name = w->getTextEditorContents ("name");
                std::unique_ptr<juce::AlertWindow> owned (w);
                if (result != 1) return;
                // the dialog can outlive this component if the host closes the editor;
                // never touch a dead this (v0.23)
                if (safeThis == nullptr) return;
                juce::String err;
                if (! safeThis->processor.presets.saveUser (name, err))
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon, "Save failed", err);
                safeThis->dirtyFlag.store (false);
                safeThis->dirty = false;
                safeThis->rebuildMenu();
            }), false);
    }

    // "..." menu (v0.34): folder reveal + USER-preset management. Factory entries show
    // the management rows disabled — they live inside the binary.
    void showOverflowMenu()
    {
        const int cur = processor.presets.getCurrentIndex();
        const auto& entries = processor.presets.getEntries();
        const bool userSel = cur >= 0 && cur < (int) entries.size()
                             && entries[(size_t) cur].isUser;
        const auto curName = userSel ? entries[(size_t) cur].name : juce::String();

        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        m.addItem (1, "Reveal preset folder in Finder");
        m.addSeparator();
        m.addItem (2, userSel ? "Overwrite \"" + curName + "\" with current sound"
                              : "Overwrite (select a USER preset)", userSel);
        m.addItem (3, "Rename...", userSel);
        m.addItem (4, "Delete...", userSel);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (revealButton),
            [safeThis = juce::Component::SafePointer<PresetBar> (this), cur, curName] (int r)
            {
                if (safeThis == nullptr || r == 0) return;
                safeThis->handleMenu (r, cur, curName);
            });
    }

    void handleMenu (int item, int cur, const juce::String& curName)
    {
        juce::String err;
        switch (item)
        {
            case 1:
            {
                auto dir = PresetManager::userDirectory();
                if (! dir.isDirectory()) dir.createDirectory();
                dir.revealToUser();
                return;
            }
            case 2:
                if (! processor.presets.overwriteUser (cur, err))
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon, "Overwrite failed", err);
                else { dirtyFlag.store (false); dirty = false; }
                rebuildMenu();
                return;
            case 3:
            {
                auto* w = new juce::AlertWindow ("Rename preset",
                                                 "New name for \"" + curName + "\":",
                                                 juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", curName, juce::String());
                w->addButton ("Rename", 1);
                w->addButton ("Cancel", 0);
                juce::Component::SafePointer<PresetBar> safeThis (this);
                w->enterModalState (true, juce::ModalCallbackFunction::create (
                    [safeThis, w, cur] (int result)
                    {
                        const auto name = w->getTextEditorContents ("name");
                        std::unique_ptr<juce::AlertWindow> owned (w);
                        if (result != 1 || safeThis == nullptr) return;
                        juce::String e;
                        if (! safeThis->processor.presets.renameUser (cur, name, e))
                            juce::AlertWindow::showMessageBoxAsync (
                                juce::MessageBoxIconType::WarningIcon, "Rename failed", e);
                        safeThis->rebuildMenu();
                    }), false);
                return;
            }
            case 4:
            {
                // explicit button ids, same pattern as promptSave/rename — the
                // MessageBoxOptions result indexing is too easy to get backwards
                auto* w = new juce::AlertWindow ("Delete preset",
                                                 "Move \"" + curName + "\" to the Trash?",
                                                 juce::MessageBoxIconType::QuestionIcon);
                w->addButton ("Delete", 1);
                w->addButton ("Cancel", 0);
                juce::Component::SafePointer<PresetBar> safeThis (this);
                w->enterModalState (true, juce::ModalCallbackFunction::create (
                    [safeThis, w, cur] (int result)
                    {
                        std::unique_ptr<juce::AlertWindow> owned (w);
                        if (result != 1 || safeThis == nullptr) return;
                        juce::String e;
                        if (! safeThis->processor.presets.deleteUser (cur, e))
                            juce::AlertWindow::showMessageBoxAsync (
                                juce::MessageBoxIconType::WarningIcon, "Delete failed", e);
                        safeThis->rebuildMenu();
                    }), false);
                return;
            }
            default: return;
        }
    }

    juce::String suggestedName() const
    {
        const int idx = processor.presets.getCurrentIndex();
        if (idx < 0) return "my sound";
        return processor.presets.getEntries()[(size_t) idx].name + " edit";
    }

    void parameterChanged (const juce::String&, float) override { dirtyFlag.store (true); }

    void timerCallback() override
    {
        if (dirtyFlag.exchange (false) && ! dirty)
        {
            dirty = true;
            showCurrent();
        }
    }

    BrokenProcessor& processor;
    juce::ComboBox presetBox;
    juce::TextButton prevButton, nextButton, saveButton, revealButton, rndButton, undoButton;
    HitPad prevButtonPad   { prevButton,   [this] { prevButton.triggerClick(); } };
    HitPad nextButtonPad   { nextButton,   [this] { nextButton.triggerClick(); } };
    HitPad revealButtonPad { revealButton, [this] { revealButton.triggerClick(); } };
    std::atomic<bool> dirtyFlag { false }; // parameterChanged can arrive off the message thread
    bool dirty = false;
    bool ignoreBoxChange = false;
};
} // namespace broken::ui
