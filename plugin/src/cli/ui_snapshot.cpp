// broken_ui_snapshot: render the editor headlessly to a PNG.
//
//   broken_ui_snapshot <out.png> [scale] [width height]
//   (scale defaults to 1.0; width/height default to the editor's constructed size --
//   pass them to gate a render at, e.g., the resize-limit minimum: setSize() bypasses the
//   constrainer entirely, same as any direct setBounds() call, so this can render sizes a
//   user could never actually drag to.)
//
// The grime layer is seeded and nothing in the panel animates, so two renders of the same
// code are byte-identical. That makes this the regression gate for look-and-feel work:
// render before, change, render after, compare.

#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include <iostream>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: broken_ui_snapshot <out.png> [scale] [width height]\n";
        return 2;
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
