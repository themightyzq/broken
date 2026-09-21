#include "PluginEditor.h"

#ifndef BROKEN_VERSION
 #define BROKEN_VERSION "dev" // defined by CMake for the plugin target
#endif

namespace ts
{
// the size the user last dragged the window to, stored on the APVTS state tree so it
// rides along with getStateInformation and comes back on reopen
static const juce::Identifier kEditorWidth ("editorWidth");

void TurboSynthEditor::Logo::paint (juce::Graphics& g)
{
    auto* lnf = dynamic_cast<ui::TsLookAndFeel*> (&getLookAndFeel());
    const juce::Font f = lnf != nullptr ? lnf->silkFont (19.0f, true).withExtraKerningFactor (0.42f)
                                        : juce::Font (juce::FontOptions (16.0f, juce::Font::bold));
    g.setFont (f);
    g.setColour (ui::colour::logoBright);
    const float baseline = ((float) getHeight() + f.getAscent() - f.getDescent()) * 0.5f;
    float x = 0.0f;
    auto width = [&f] (const juce::String& t) { return juce::GlyphArrangement::getStringWidth (f, t); };

    g.drawSingleLineText ("BRO", (int) x, (int) baseline);
    x += width ("BRO");
    {
        // the K, flipped about its own centre so it reads backwards
        const float kw = width ("K");
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, x + kw * 0.5f, baseline));
        g.drawSingleLineText ("K", (int) x, (int) baseline);
        x += kw;
    }
    g.drawSingleLineText ("EN", (int) x, (int) baseline);

}

void TurboSynthEditor::AboutOverlay::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.72f)); // dim the rack behind
    g.fillRect (full);

    auto panel = getLocalBounds().withSizeKeepingCentre (560, 360).toFloat();
    g.setGradientFill (ui::gradients::panel (panel));
    g.fillRect (panel);
    g.setColour (ui::colour::panelBorder);
    g.drawRect (panel, 1.0f);

    auto* lnf = dynamic_cast<ui::TsLookAndFeel*> (&getLookAndFeel());
    auto r = panel.reduced (28.0f).toNearestInt();

    g.setColour (ui::colour::silkTitle);
    g.setFont (lnf != nullptr ? lnf->silkFont (22.0f, true).withExtraKerningFactor (0.3f)
                              : juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText ("BROKEN", r.removeFromTop (30), juce::Justification::centredLeft);

    g.setFont (lnf != nullptr ? lnf->silkFont (13.0f, false)
                              : juce::Font (juce::FontOptions (12.0f)));
    g.setColour (ui::colour::silkLabel);
    const char* lines[] = {
        "Version " BROKEN_VERSION "  \xc2\xb7  ZQ SFX",
        "",
        "A sample mangler in the spirit of the Digidesign TurboSynth,",
        "as heard all over early-90s industrial records.",
        "",
        "Free software under GPLv3 \xe2\x80\x94 source available from ZQ SFX.",
        "",
        "Knob filmstrips: Analog Knob Kit 01 by Julian Behrens (Noisehead).",
        "Fonts: Barlow Condensed, VT323, IBM Plex Mono (SIL OFL).",
        "",
        "Not affiliated with a third party, Digidesign, or the band.",
        "",
        "Click anywhere to close.",
    };
    for (auto* line : lines)
        g.drawText (juce::String::fromUTF8 (line), r.removeFromTop (22),
                    juce::Justification::centredLeft);
}

void TurboSynthEditor::Content::paint (juce::Graphics& g)
{
    g.setGradientFill (ts::ui::gradients::chassis (getLocalBounds().toFloat()));
    g.fillAll();
    g.setColour (ui::colour::ruleTitle);
    g.drawLine (0.0f, (float) headerH, (float) getWidth(), (float) headerH, 1.0f);

}

void TurboSynthEditor::Grime::paint (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    if (! cache.isValid() || cache.getWidth() != w || cache.getHeight() != h)
    {
        // Bake once per size. JUCE has no CSS "overlay" blend; a pre-baked low-alpha
        // noise field reads the same at this opacity (deliberate approximation of the
        // reference). Seeded, so the grime pattern is stable across repaints.
        cache = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics ig (cache);
        juce::Random rng (0xB2073); // fixed seed: the grime never shifts between repaints
        for (int i = 0; i < w * h / 48; ++i)
        {
            const int px = rng.nextInt (w), py = rng.nextInt (h);
            const bool bright = rng.nextBool();
            ig.setColour ((bright ? juce::Colours::white : juce::Colours::black)
                              .withAlpha (0.01f + rng.nextFloat() * 0.03f));
            ig.fillRect (px, py, 1 + rng.nextInt (2), 1);
        }
        // vignette
        juce::ColourGradient vig (juce::Colours::transparentBlack, (float) w * 0.5f, (float) h * 0.45f,
                                  juce::Colours::black.withAlpha (0.18f), 0.0f, 0.0f, true);
        ig.setGradientFill (vig);
        ig.fillRect (0, 0, w, h);
        // two corner blotches
        for (auto c : { juce::Point<float> (w * 0.06f, h * 0.94f), juce::Point<float> (w * 0.96f, h * 0.08f) })
        {
            juce::ColourGradient b (juce::Colours::black.withAlpha (0.12f), c.x, c.y,
                                    juce::Colours::transparentBlack, c.x + w * 0.12f, c.y + w * 0.12f, true);
            ig.setGradientFill (b);
            ig.fillEllipse (c.x - w * 0.12f, c.y - w * 0.12f, w * 0.24f, w * 0.24f);
        }
        // four slot-head screws, random slot angles (seeded)
        for (auto p : { juce::Point<float> (10.0f, 10.0f), { (float) w - 10.0f, 10.0f },
                        { 10.0f, (float) h - 10.0f }, { (float) w - 10.0f, (float) h - 10.0f } })
        {
            const float r = 6.0f;
            juce::ColourGradient steel (juce::Colour (0xff9aa0a4), p.x - 2, p.y - 2,
                                        juce::Colour (0xff3c4043), p.x + r, p.y + r, true);
            ig.setGradientFill (steel);
            ig.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
            ig.setColour (juce::Colours::black.withAlpha (0.8f));
            ig.drawEllipse (p.x - r, p.y - r, r * 2, r * 2, 1.0f);
            const float a = rng.nextFloat() * juce::MathConstants<float>::pi;
            ig.drawLine (p.x - std::cos (a) * (r - 1.5f), p.y - std::sin (a) * (r - 1.5f),
                         p.x + std::cos (a) * (r - 1.5f), p.y + std::sin (a) * (r - 1.5f), 1.6f);
        }
    }
    g.drawImageAt (cache, 0, 0);
}

void TurboSynthEditor::Content::resized()
{
    if (owner == nullptr) return;
    auto area = getLocalBounds();

    auto top = area.removeFromTop (headerH);
    owner->title.setBounds (top.removeFromLeft (150).withTrimmedLeft (14));
    // preset cluster hugs the right edge at a sane width; the gap in between stays chassis
    auto bar = top.reduced (6, 4);
    owner->presetBar.setBounds (bar.removeFromRight (juce::jmin (620, bar.getWidth())));

    area.removeFromTop (4);
    owner->mangleView.setBounds (area.removeFromTop (mangleH));
    area.removeFromTop (4);
    owner->editView.setBounds (area.removeFromTop (editH));

    // grime paints over EVERYTHING in the content (it never takes the mouse)
    owner->grime.setBounds (getLocalBounds());
    owner->grime.setVisible (true);
    owner->grime.toFront (false);
    owner->about.setBounds (getLocalBounds());
    owner->about.toFront (false); // above the grime: it must eat clicks while open
}

TurboSynthEditor::TurboSynthEditor (TurboSynthProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      tooltipWindow (this, 500),
      presetBar (p),
      mangleView (p),
      editView (p)
{
    setLookAndFeel (&lookAndFeel);


    content.owner = this;
    content.addAndMakeVisible (title);
    title.setTooltip ("About Broken \xe2\x80\x94 version, licence, credits.");
    title.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    title.onClick = [this] { about.setVisible (true); about.toFront (false); };
    content.addChildComponent (about); // hidden until the logo is clicked
    content.addChildComponent (grime);   // made visible + sized in Content::resized
    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (mangleView);
    content.addAndMakeVisible (editView);   // no toggle: both are always visible now
    addAndMakeVisible (content);

    // Uniform scaling keeps every block's internal layout in design coordinates, so the
    // aspect ratio is locked; the alternative is maintaining a layout per size.
    constrainer.setFixedAspectRatio ((double) designW / (double) designH);
    constrainer.setSizeLimits ((int) (designW * 0.55), (int) (designH * 0.55),
                               (int) (designW * 1.75), (int) (designH * 1.75));
    setConstrainer (&constrainer);
    setResizable (true, true);

    // 55% keeps the whole panel usable on a 1512x982 laptop, which matters for release
    int w = designW;
    if (auto v = proc.apvts.state.getProperty (kEditorWidth); ! v.isVoid())
        w = juce::jlimit ((int) (designW * 0.55), (int) (designW * 1.75), (int) v);
    setSize (w, juce::roundToInt ((double) w * designH / designW));
}

TurboSynthEditor::~TurboSynthEditor()
{
    setConstrainer (nullptr);
    setLookAndFeel (nullptr);
}

void TurboSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colour::bg); // behind the scaled content, for any rounding sliver
}

void TurboSynthEditor::resized()
{
    const double scale = (double) getWidth() / (double) designW;
    content.setTransform (juce::AffineTransform::scale ((float) scale));
    content.setBounds (0, 0, designW, designH); // always design size; the transform sizes it

    proc.apvts.state.setProperty (kEditorWidth, getWidth(), nullptr);
}
} // namespace ts
