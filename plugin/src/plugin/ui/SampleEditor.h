#pragma once
// Soundminer-style live region editor overlay. No preview button by design: a MIDI key
// is the preview, and every drag writes sample.regstart/regend LIVE via the APVTS
// parameters (beginChangeGesture on mouseDown, setValueNotifyingHost per drag,
// endChangeGesture on mouseUp) so the sound updates while you drag.
//
// Covers the whole MangleView when open (MangleView owns + shows/hides it).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "Controls.h"
#include "TsLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../Params.h"

namespace ts::ui
{
// ---------------------------------------------------------------------------------
// Peak pyramid: max/min per block at four block sizes, built once from the mono
// sample buffer. Drawing picks the finest level whose block size does not exceed the
// current samples-per-pixel (closest-but-finer); when even the finest level (256) is
// coarser than the view, the caller falls back to reading raw samples per column.
// ---------------------------------------------------------------------------------
struct PeakPyramid
{
    struct Level
    {
        int blockSize = 0;
        std::vector<float> mins, maxs; // one entry per block, preallocated at build time
    };

    std::vector<Level> levels; // ascending block size: 256, 1024, 4096, 16384
    size_t sampleCount = 0;

    void build (const std::vector<float>& buf)
    {
        static constexpr int blockSizes[] = { 256, 1024, 4096, 16384 };
        sampleCount = buf.size();
        levels.clear();
        levels.reserve (4);

        for (int bs : blockSizes)
        {
            Level lvl;
            lvl.blockSize = bs;
            size_t nBlocks = buf.empty() ? 0 : (buf.size() + (size_t) bs - 1) / (size_t) bs;
            lvl.mins.assign (nBlocks, 0.0f);
            lvl.maxs.assign (nBlocks, 0.0f);

            for (size_t b = 0; b < nBlocks; ++b)
            {
                size_t s0 = b * (size_t) bs;
                size_t s1 = std::min (buf.size(), s0 + (size_t) bs);
                float mn = 0.0f, mx = 0.0f;
                for (size_t i = s0; i < s1; ++i)
                {
                    mn = std::min (mn, buf[i]);
                    mx = std::max (mx, buf[i]);
                }
                lvl.mins[b] = mn;
                lvl.maxs[b] = mx;
            }
            levels.push_back (std::move (lvl));
        }
    }

    const Level* pickLevel (double samplesPerPixel) const
    {
        const Level* best = nullptr;
        for (auto& lvl : levels)
            if ((double) lvl.blockSize <= samplesPerPixel
                && (best == nullptr || lvl.blockSize > best->blockSize))
                best = &lvl;
        return best;
    }
};

// Builds a PeakPyramid off the message thread for large files (>= ~2M samples);
// smaller files build inline on the calling (message) thread. The worker only ever
// touches its own private copy of the buffer, never the processor's live vector, so
// there is no race with loadSampleFile() swapping sampleBuf on the message thread.
class PyramidBuilder : private juce::Thread
{
public:
    PyramidBuilder() : juce::Thread ("ts-pyramid-build") {}
    ~PyramidBuilder() override { stopThread (2000); }

    void start (const std::vector<float>& buf)
    {
        // if the previous build won't stop, DON'T reassign snapshot/result under it —
        // the worker is still reading them (v0.12 review)
        if (! stopThread (2000))
            return;
        ready.store (false);
        result.reset();
        snapshot = buf;

        if (snapshot.size() < syncThreshold)
        {
            auto p = std::make_unique<PeakPyramid>();
            p->build (snapshot);
            result = std::move (p);
            ready.store (true);
        }
        else
        {
            startThread (juce::Thread::Priority::background);
        }
    }

    bool isReady() const { return ready.load(); }

    std::unique_ptr<PeakPyramid> takeResult()
    {
        ready.store (false);
        return std::move (result);
    }

private:
    void run() override
    {
        auto p = std::make_unique<PeakPyramid>();
        p->build (snapshot);
        if (threadShouldExit())
            return;
        result = std::move (p);
        ready.store (true);
    }

    static constexpr size_t syncThreshold = 2'000'000;
    std::vector<float> snapshot;
    std::unique_ptr<PeakPyramid> result;
    std::atomic<bool> ready { false };
};

// Shared live state between the waveform area and the minimap.
struct EditorState
{
    explicit EditorState (TurboSynthProcessor& p) : processor (p) {}

    TurboSynthProcessor& processor;
    std::unique_ptr<PeakPyramid> pyramid;
    double viewStart01 = 0.0;
    double viewEnd01 = 1.0;
    bool snapEnabled = true;
};

// sample.regstart/regend are 0..1 ranges, but go through convertTo0to1/From0to1 rather
// than assuming identity, in case that range ever changes.
inline float regGet (const juce::RangedAudioParameter& p)
{
    return p.convertFrom0to1 (p.getValue());
}

inline void regSet (juce::RangedAudioParameter& p, double normFileValue)
{
    p.setValueNotifyingHost (p.convertTo0to1 ((float) juce::jlimit (0.0, 1.0, normFileValue)));
}

// ---------------------------------------------------------------------------------
// Main waveform: zoom/pan, selection drag (create/move/resize edges), playhead.
// ---------------------------------------------------------------------------------
class WaveArea : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveArea (EditorState& s) : state (s)
    {
        setWantsKeyboardFocus (false);
        setTooltip ("Drag to select the region that plays. Scroll zooms, drag the "
                    "background pans.");
    }

    std::function<void()> onChanged; // fired after any view or region mutation

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        g.setColour (colour::lcdBg);
        g.fillRect (b);
        g.setColour (colour::lcdBorder);
        g.drawRect (b, 1);

        const auto& buf = state.processor.getDisplayBuffer();
        auto area = b.reduced (1);
        const int w = area.getWidth();
        const int h = area.getHeight();
        if (w <= 0 || h <= 0)
            return;

        if (buf.empty())
        {
            g.setColour (colour::lcdFaint);
            g.setFont (lcdPlaceholderFont());
            g.drawText ("NO SAMPLE LOADED", b, juce::Justification::centred);
            return;
        }

        if (state.pyramid == nullptr)
        {
            g.setColour (colour::lcdFaint);
            g.setFont (lcdPlaceholderFont());
            g.drawText ("building waveform...", b, juce::Justification::centred);
            return;
        }

        const int midY = area.getCentreY();
        const double n = (double) buf.size();
        const double viewWidth = juce::jmax (1.0 / n, state.viewEnd01 - state.viewStart01);
        const double samplesPerPixel = (viewWidth * n) / (double) w;

        g.setColour (colour::lcdDim);
        for (int x = 0; x < w; ++x)
        {
            double t0 = state.viewStart01 + (viewWidth * x) / (double) w;
            double t1 = state.viewStart01 + (viewWidth * (x + 1)) / (double) w;
            size_t i0 = (size_t) juce::jlimit (0.0, n, t0 * n);
            size_t i1 = (size_t) juce::jlimit (0.0, n, t1 * n);
            float mn = 0.0f, mx = 0.0f;
            minMaxForRange (buf, i0, i1, samplesPerPixel, mn, mx);
            int y0 = midY - (int) (mx * (float) h * 0.5f);
            int y1 = midY - (int) (mn * (float) h * 0.5f);
            if (y0 > y1) std::swap (y0, y1);
            if (y0 == y1) ++y1;
            g.drawVerticalLine (area.getX() + x, (float) y0, (float) y1);
        }

        auto sel = selectionPixelRange (w);
        if (sel.getLength() > 0)
        {
            g.setColour (colour::lcdText.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<int> (area.getX() + sel.getStart(), area.getY(),
                                               sel.getLength(), h));
            g.setColour (colour::lcdText);
            g.drawVerticalLine (area.getX() + sel.getStart(), (float) area.getY(), (float) area.getBottom());
            g.drawVerticalLine (area.getX() + juce::jmax (sel.getStart(), sel.getEnd() - 1),
                                 (float) area.getY(), (float) area.getBottom());
        }

        // Crossfade zone wedge(s): only meaningful while looping. Read the four params
        // as cheap atomics every paint (per spec) rather than caching, since none of
        // this is on a hot path. loopstyle index 1 == "PingPong" (params::loopStyles).
        if (auto* loopOnP = state.processor.apvts.getRawParameterValue ("sample.loopon"))
        {
            if (loopOnP->load() > 0.5f)
            {
                auto* rs = regStartParam();
                auto* re = regEndParam();
                auto* styleP = state.processor.apvts.getRawParameterValue ("sample.loopstyle");
                auto* revP   = state.processor.apvts.getRawParameterValue ("sample.rev");
                auto* xfadeP = state.processor.apvts.getRawParameterValue ("sample.xfade");

                if (rs != nullptr && re != nullptr && styleP != nullptr && revP != nullptr && xfadeP != nullptr)
                {
                    double regS = (double) regGet (*rs);
                    double regE = (double) regGet (*re);
                    if (regE > regS && n > 0.0)
                    {
                        bool pingPong = ((int) styleP->load()) == 1;
                        bool rev = revP->load() > 0.5f;
                        double fileSr = juce::jmax (1.0, state.processor.getDisplaySr());
                        double xfadeMs = (double) xfadeP->load();
                        double xNorm = juce::jlimit (0.0, regE - regS,
                                                      ((xfadeMs / 1000.0) * fileSr) / n);

                        g.setColour (colour::lcdText.withAlpha (geom::dimAlpha));

                        // outerNorm is the region boundary the fade touches (tapers to a
                        // point there); innerNorm is the far edge of the fade zone (full
                        // height there) — this draws a right-triangle "wedge" shape.
                        auto drawWedge = [&] (double outerNorm, double innerNorm)
                        {
                            double lo = std::min (outerNorm, innerNorm);
                            double hi = std::max (outerNorm, innerNorm);
                            int xLo = normToX (lo, w);
                            int xHi = normToX (hi, w);
                            if (xHi <= xLo)
                                return; // fully outside the visible view

                            float top = (float) area.getY();
                            float bottom = (float) area.getBottom();
                            float mid = (top + bottom) * 0.5f;
                            juce::Path wedge;
                            if (outerNorm < innerNorm) // apex at the left edge (START fade)
                            {
                                wedge.startNewSubPath ((float) (area.getX() + xLo), mid);
                                wedge.lineTo ((float) (area.getX() + xHi), top);
                                wedge.lineTo ((float) (area.getX() + xHi), bottom);
                            }
                            else // apex at the right edge (END fade)
                            {
                                wedge.startNewSubPath ((float) (area.getX() + xLo), top);
                                wedge.lineTo ((float) (area.getX() + xLo), bottom);
                                wedge.lineTo ((float) (area.getX() + xHi), mid);
                            }
                            wedge.closeSubPath();
                            g.fillPath (wedge);
                        };

                        if (pingPong)
                        {
                            drawWedge (regS, regS + xNorm);       // start-of-region fade
                            drawWedge (regE, regE - xNorm);       // end-of-region fade
                        }
                        else if (rev)
                        {
                            drawWedge (regS, regS + xNorm);
                        }
                        else
                        {
                            drawWedge (regE, regE - xNorm);
                        }
                    }
                }
            }
        }

        float ph = state.processor.getPlayhead01();
        if (ph >= (float) state.viewStart01 && ph <= (float) state.viewEnd01)
        {
            int px = area.getX() + (int) (((double) ph - state.viewStart01) / viewWidth * w);
            g.setColour (colour::lcdText);
            g.drawVerticalLine (px, (float) area.getY(), (float) area.getBottom());
        }
    }

    void resized() override {}

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (cursorForPosition (e.x));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int w = getWidth();
        if (w <= 0)
            return;

        if (e.mods.isShiftDown())
        {
            dragMode = Drag::Pan;
            panAnchorX = e.x;
            panAnchorViewStart = state.viewStart01;
            panAnchorViewEnd = state.viewEnd01;
            return;
        }

        auto* rs = regStartParam();
        auto* re = regEndParam();
        if (rs == nullptr || re == nullptr)
            return;

        double t = xToNorm (e.x, w);
        float rsv = regGet (*rs);
        float rev = regGet (*re);
        int xs = normToX (rsv, w);
        int xe = normToX (rev, w);

        constexpr int hit = 6;
        if (std::abs (e.x - xs) <= hit)
            dragMode = Drag::ResizeLeft;
        else if (std::abs (e.x - xe) <= hit)
            dragMode = Drag::ResizeRight;
        else if (e.x > xs && e.x < xe)
            dragMode = Drag::Move;
        else
            dragMode = Drag::Create;

        switch (dragMode)
        {
            case Drag::ResizeLeft:
                rs->beginChangeGesture();
                break;
            case Drag::ResizeRight:
                re->beginChangeGesture();
                break;
            case Drag::Move:
                rs->beginChangeGesture();
                re->beginChangeGesture();
                moveAnchorNorm = t;
                moveAnchorStart = rsv;
                moveAnchorEnd = rev;
                break;
            case Drag::Create:
                rs->beginChangeGesture();
                re->beginChangeGesture();
                createAnchor = t;
                regSet (*rs, t);
                regSet (*re, t);
                break;
            case Drag::None:
            case Drag::Pan:
                break;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int w = getWidth();
        if (w <= 0)
            return;

        if (dragMode == Drag::Pan)
        {
            double dxNorm = ((double) (e.x - panAnchorX) / (double) w)
                             * (panAnchorViewEnd - panAnchorViewStart);
            double vw = panAnchorViewEnd - panAnchorViewStart;
            double newStart = juce::jlimit (0.0, 1.0 - vw, panAnchorViewStart - dxNorm);
            state.viewStart01 = newStart;
            state.viewEnd01 = newStart + vw;
            repaint();
            if (onChanged) onChanged();
            return;
        }

        auto* rs = regStartParam();
        auto* re = regEndParam();
        if (rs == nullptr || re == nullptr)
            return;

        double t = juce::jlimit (0.0, 1.0, xToNorm (e.x, w));

        if (dragMode == Drag::ResizeLeft)
        {
            double limit = (double) regGet (*re) - 0.0001;
            regSet (*rs, juce::jmin (t, limit));
        }
        else if (dragMode == Drag::ResizeRight)
        {
            double limit = (double) regGet (*rs) + 0.0001;
            regSet (*re, juce::jmax (t, limit));
        }
        else if (dragMode == Drag::Move)
        {
            double len = moveAnchorEnd - moveAnchorStart;
            double delta = t - moveAnchorNorm;
            double ns = juce::jlimit (0.0, 1.0 - len, moveAnchorStart + delta);
            regSet (*rs, ns);
            regSet (*re, ns + len);
        }
        else if (dragMode == Drag::Create)
        {
            regSet (*rs, juce::jmin (createAnchor, t));
            regSet (*re, juce::jmax (createAnchor, t));
        }
        else
        {
            return;
        }

        repaint();
        if (onChanged) onChanged();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        auto* rs = regStartParam();
        auto* re = regEndParam();

        if (dragMode == Drag::ResizeLeft || dragMode == Drag::Create || dragMode == Drag::Move)
            if (rs != nullptr && state.snapEnabled) snapParam (*rs);
        if (dragMode == Drag::ResizeRight || dragMode == Drag::Create || dragMode == Drag::Move)
            if (re != nullptr && state.snapEnabled) snapParam (*re);

        if (dragMode == Drag::ResizeLeft && rs != nullptr)
            rs->endChangeGesture();
        else if (dragMode == Drag::ResizeRight && re != nullptr)
            re->endChangeGesture();
        else if (dragMode == Drag::Move || dragMode == Drag::Create)
        {
            if (rs != nullptr) rs->endChangeGesture();
            if (re != nullptr) re->endChangeGesture();
        }

        dragMode = Drag::None;
        repaint();
        if (onChanged) onChanged();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto& buf = state.processor.getDisplayBuffer();
        if (buf.empty() || getWidth() <= 0 || wheel.deltaY == 0.0f)
            return;

        double cursorNorm = xToNorm (e.x, getWidth());
        double factor = wheel.deltaY > 0.0f ? 1.2 : (1.0 / 1.2);
        zoomAt (factor, cursorNorm, buf.size());
        repaint();
        if (onChanged) onChanged();
    }

private:
    // The LnF installed on this component can briefly still be the JUCE default (before
    // the plugin editor installs TsLookAndFeel on the tree) — fall back rather than crash
    // on a null cast.
    juce::Font lcdPlaceholderFont() const
    {
        if (auto* lnf = dynamic_cast<TsLookAndFeel*> (&getLookAndFeel()))
            return lnf->lcdFont (14.0f);
        return juce::Font (juce::FontOptions (13.0f));
    }

    enum class Drag { None, ResizeLeft, ResizeRight, Move, Create, Pan };

    juce::RangedAudioParameter* regStartParam() const
    {
        return state.processor.apvts.getParameter ("sample.regstart");
    }
    juce::RangedAudioParameter* regEndParam() const
    {
        return state.processor.apvts.getParameter ("sample.regend");
    }

    double xToNorm (int x, int w) const
    {
        double vw = state.viewEnd01 - state.viewStart01;
        return state.viewStart01 + ((double) x / (double) w) * vw;
    }

    int normToX (double norm, int w) const
    {
        double vw = state.viewEnd01 - state.viewStart01;
        if (vw <= 0.0)
            return 0;
        return (int) juce::jlimit (0.0, (double) w, ((norm - state.viewStart01) / vw) * (double) w);
    }

    juce::Range<int> selectionPixelRange (int w) const
    {
        auto* rs = regStartParam();
        auto* re = regEndParam();
        if (rs == nullptr || re == nullptr)
            return {};

        double s = (double) regGet (*rs);
        double e = (double) regGet (*re);
        if (e <= s)
            return {};

        double vs = state.viewStart01, ve = state.viewEnd01;
        s = juce::jlimit (vs, ve, s);
        e = juce::jlimit (vs, ve, e);
        if (e <= s)
            return {};

        int xs = normToX (s, w);
        int xe = normToX (e, w);
        return { xs, juce::jmax (xs + 1, xe) };
    }

    juce::MouseCursor cursorForPosition (int x) const
    {
        auto* rs = regStartParam();
        auto* re = regEndParam();
        int w = getWidth();
        if (rs == nullptr || re == nullptr || w <= 0)
            return juce::MouseCursor::NormalCursor;

        int xs = normToX (regGet (*rs), w);
        int xe = normToX (regGet (*re), w);
        constexpr int hit = 6;
        if (std::abs (x - xs) <= hit || std::abs (x - xe) <= hit)
            return juce::MouseCursor::LeftRightResizeCursor;
        if (x > xs && x < xe)
            return juce::MouseCursor::DraggingHandCursor;
        return juce::MouseCursor::NormalCursor;
    }

    void minMaxForRange (const std::vector<float>& buf, size_t i0, size_t i1,
                          double samplesPerPixel, float& outMin, float& outMax) const
    {
        outMin = 0.0f;
        outMax = 0.0f;
        if (buf.empty())
            return;
        i1 = std::min (buf.size(), std::max (i1, i0 + 1));
        if (i0 >= buf.size())
            return;

        const PeakPyramid::Level* lvl = state.pyramid != nullptr
                                             ? state.pyramid->pickLevel (samplesPerPixel)
                                             : nullptr;
        if (lvl == nullptr || lvl->mins.empty())
        {
            for (size_t i = i0; i < i1; ++i)
            {
                outMin = std::min (outMin, buf[i]);
                outMax = std::max (outMax, buf[i]);
            }
            return;
        }

        size_t b0 = std::min (i0 / (size_t) lvl->blockSize, lvl->mins.size() - 1);
        size_t b1 = std::min ((i1 - 1) / (size_t) lvl->blockSize, lvl->mins.size() - 1);
        for (size_t b = b0; b <= b1; ++b)
        {
            outMin = std::min (outMin, lvl->mins[b]);
            outMax = std::max (outMax, lvl->maxs[b]);
        }
    }

    void snapParam (juce::RangedAudioParameter& param) const
    {
        const auto& buf = state.processor.getDisplayBuffer();
        if (buf.size() < 2)
            return;

        double norm = (double) regGet (param);
        size_t idx = (size_t) juce::jlimit (0.0, (double) (buf.size() - 1), norm * (double) (buf.size() - 1));
        // the FILE's native rate — a 44.1k recording in a 48k session must still snap
        // within a true 20 ms window of its own timeline
        double sr = juce::jmax (1.0, state.processor.getDisplaySr());
        size_t window = (size_t) (0.020 * sr);
        size_t lo = idx > window ? idx - window : 0;
        size_t hi = std::min (buf.size() - 1, idx + window);

        size_t best = idx;
        double bestDist = 1.0e18;
        for (size_t i = lo; i < hi; ++i)
        {
            float a = buf[i], b = buf[i + 1];
            if ((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f))
            {
                size_t cross = std::abs (a) < std::abs (b) ? i : (i + 1);
                double dist = std::abs ((double) cross - (double) idx);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    best = cross;
                }
            }
        }
        regSet (param, (double) best / (double) (buf.size() - 1));
    }

    void zoomAt (double factor, double cursorNorm, size_t bufSize)
    {
        double curWidth = state.viewEnd01 - state.viewStart01;
        double minWidth = bufSize > 0 ? juce::jmin (1.0, 256.0 / (double) bufSize) : 1.0;
        double newWidth = juce::jlimit (minWidth, 1.0, curWidth / factor);
        double frac = curWidth > 0.0 ? (cursorNorm - state.viewStart01) / curWidth : 0.5;
        double newStart = cursorNorm - frac * newWidth;
        double newEnd = newStart + newWidth;
        if (newStart < 0.0) { newEnd -= newStart; newStart = 0.0; }
        if (newEnd > 1.0) { newStart -= (newEnd - 1.0); newEnd = 1.0; }
        state.viewStart01 = juce::jmax (0.0, newStart);
        state.viewEnd01 = juce::jmin (1.0, state.viewStart01 + newWidth);
    }

    EditorState& state;
    Drag dragMode = Drag::None;
    double moveAnchorNorm = 0.0, moveAnchorStart = 0.0, moveAnchorEnd = 0.0;
    double createAnchor = 0.0;
    int panAnchorX = 0;
    double panAnchorViewStart = 0.0, panAnchorViewEnd = 0.0;
};

// ---------------------------------------------------------------------------------
// Minimap: whole-file overview from the pyramid's coarsest level, region tint, and a
// draggable viewport rectangle (click outside it jumps; drag it/inside it pans).
// ---------------------------------------------------------------------------------
class Minimap : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Minimap (EditorState& s) : state (s)
    {
        setTooltip ("The whole file. Drag the box to move the view.");
    }

    std::function<void()> onChanged;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        g.setColour (colour::lcdBg);
        g.fillRect (b);
        g.setColour (colour::lcdBorder);
        g.drawRect (b, 1);

        const auto& buf = state.processor.getDisplayBuffer();
        auto area = b.reduced (1);
        int w = area.getWidth(), h = area.getHeight();
        if (buf.empty() || w <= 0 || h <= 0)
            return;

        int midY = area.getCentreY();
        double n = (double) buf.size();

        if (state.pyramid != nullptr && ! state.pyramid->levels.empty())
        {
            const auto& top = state.pyramid->levels.back(); // coarsest block size for the overview
            g.setColour (colour::lcdDim);
            for (int x = 0; x < w; ++x)
            {
                size_t i0 = (size_t) ((double) x / (double) w * n);
                size_t i1 = std::min (buf.size(), std::max ((size_t) ((double) (x + 1) / (double) w * n), i0 + 1));
                float mn = 0.0f, mx = 0.0f;
                if (! top.mins.empty())
                {
                    size_t b0 = std::min (i0 / (size_t) top.blockSize, top.mins.size() - 1);
                    size_t b1 = std::min ((i1 - 1) / (size_t) top.blockSize, top.mins.size() - 1);
                    for (size_t bIdx = b0; bIdx <= b1; ++bIdx)
                    {
                        mn = std::min (mn, top.mins[bIdx]);
                        mx = std::max (mx, top.maxs[bIdx]);
                    }
                }
                int y0 = midY - (int) (mx * (float) h * 0.5f);
                int y1 = midY - (int) (mn * (float) h * 0.5f);
                if (y0 > y1) std::swap (y0, y1);
                if (y0 == y1) ++y1;
                g.drawVerticalLine (area.getX() + x, (float) y0, (float) y1);
            }
        }

        if (auto* rs = regStartParam())
        {
            if (auto* re = regEndParam())
            {
                double s = (double) regGet (*rs), e = (double) regGet (*re);
                if (e > s)
                {
                    int xs = (int) (s * w), xe = (int) (e * w);
                    // same region tint as the WaveArea's selection fill, for one visual
                    // language across both screens
                    g.setColour (colour::lcdText.withAlpha (0.18f));
                    g.fillRect (juce::Rectangle<int> (area.getX() + xs, area.getY(),
                                                        juce::jmax (1, xe - xs), h));
                }
            }
        }

        int vx0 = (int) (state.viewStart01 * w);
        int vx1 = (int) (state.viewEnd01 * w);
        g.setColour (colour::silkLabel);
        g.drawRect (juce::Rectangle<float> ((float) (area.getX() + vx0), (float) area.getY(),
                                             (float) juce::jmax (1, vx1 - vx0), (float) h), 1.5f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int w = getWidth();
        if (w <= 0)
            return;

        double vw = state.viewEnd01 - state.viewStart01;
        int vx0 = (int) (state.viewStart01 * w);
        int vx1 = (int) (state.viewEnd01 * w);

        draggingViewport = true;
        if (e.x >= vx0 && e.x <= vx1)
        {
            dragStartX = e.x;
            dragStartViewStart = state.viewStart01;
        }
        else
        {
            double centre = (double) e.x / (double) w;
            double newStart = juce::jlimit (0.0, 1.0 - vw, centre - vw * 0.5);
            state.viewStart01 = newStart;
            state.viewEnd01 = newStart + vw;
            dragStartX = e.x;
            dragStartViewStart = newStart;
            repaint();
            if (onChanged) onChanged();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! draggingViewport)
            return;
        int w = getWidth();
        if (w <= 0)
            return;

        double vw = state.viewEnd01 - state.viewStart01;
        double dNorm = (double) (e.x - dragStartX) / (double) w;
        double newStart = juce::jlimit (0.0, 1.0 - vw, dragStartViewStart + dNorm);
        state.viewStart01 = newStart;
        state.viewEnd01 = newStart + vw;
        repaint();
        if (onChanged) onChanged();
    }

    void mouseUp (const juce::MouseEvent&) override { draggingViewport = false; }

private:
    juce::RangedAudioParameter* regStartParam() const
    {
        return state.processor.apvts.getParameter ("sample.regstart");
    }
    juce::RangedAudioParameter* regEndParam() const
    {
        return state.processor.apvts.getParameter ("sample.regend");
    }

    EditorState& state;
    bool draggingViewport = false;
    int dragStartX = 0;
    double dragStartViewStart = 0.0;
};

// ---------------------------------------------------------------------------------
// SampleEditor: the overlay itself. Owned + shown/hidden by MangleView.
// ---------------------------------------------------------------------------------
class SampleEditor : public juce::Component, private juce::Timer
{
public:
    explicit SampleEditor (TurboSynthProcessor& p) : processor (p), state (p)
    {
        setWantsKeyboardFocus (true);

        nameLabel.setJustificationType (juce::Justification::centredLeft);
        nameLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        addAndMakeVisible (nameLabel);

        readoutLabel.setJustificationType (juce::Justification::centredLeft);
        readoutLabel.setColour (juce::Label::textColourId, colour::lcdDim);
        applyReadoutFont(); // may still be the JUCE default LnF this early; corrected below
        addAndMakeVisible (readoutLabel);

        closeButton.setTooltip ("Close the region editor.");
        closeButton.onClick = [this] { closeEditor(); };
        addAndMakeVisible (closeButton);

        fitButton.setTooltip ("Reset zoom to the full file.");
        fitButton.onClick = [this]
        {
            state.viewStart01 = 0.0;
            state.viewEnd01 = 1.0;
            waveArea->repaint();
            minimap->repaint();
        };
        addAndMakeVisible (fitButton);

        snapButton.setClickingTogglesState (true);
        snapButton.setToggleState (true, juce::dontSendNotification);
        snapButton.setTooltip ("Snaps selection edges to zero crossings on release.");
        snapButton.onClick = [this] { state.snapEnabled = snapButton.getToggleState(); };
        addAndMakeVisible (snapButton);

        clearButton.setTooltip ("Resets the region to the full file.");
        clearButton.onClick = [this]
        {
            if (auto* rs = processor.apvts.getParameter ("sample.regstart"))
            {
                rs->beginChangeGesture();
                regSet (*rs, 0.0);
                rs->endChangeGesture();
            }
            if (auto* re = processor.apvts.getParameter ("sample.regend"))
            {
                re->beginChangeGesture();
                regSet (*re, 1.0);
                re->endChangeGesture();
            }
            waveArea->repaint();
            minimap->repaint();
            updateReadouts();
        };
        addAndMakeVisible (clearButton);

        loopToggle = std::make_unique<TextToggle> (processor.apvts, "sample.loopon", "LOOP",
            "Off = one-shot. On = the region repeats while you hold the key; style and "
            "XFADE light up.");
        addAndMakeVisible (loopToggle.get());

        styleCombo = std::make_unique<Combo> (processor.apvts, "sample.loopstyle", params::loopStyles,
            "STYLE", "Wrap back to the start, or bounce end-to-end.");
        addAndMakeVisible (styleCombo.get());

        revToggle = std::make_unique<TextToggle> (processor.apvts, "sample.rev", "REV",
            "Reverses the source, Soundminer-style. Works with everything "
            "\xe2\x80\x94 one-shots, loops, bounces.");
        addAndMakeVisible (revToggle.get());

        xfadeKnob = std::make_unique<Knob> (processor.apvts, "sample.xfade", "XFADE",
            "Smooths the loop seam and the ping-pong turnaround. 0 = the raw era click. "
            "Shown as a shaded wedge on the waveform.", false, true, "ms");
        addAndMakeVisible (xfadeKnob.get());

        zoomSelButton.setTooltip ("Frames the current selection for loop-point surgery.");
        zoomSelButton.onClick = [this]
        {
            auto* rs = processor.apvts.getParameter ("sample.regstart");
            auto* re = processor.apvts.getParameter ("sample.regend");
            if (rs == nullptr || re == nullptr)
                return;
            double s = (double) regGet (*rs);
            double e = (double) regGet (*re);
            double margin = juce::jmax (0.0, e - s) * 0.1;
            state.viewStart01 = juce::jlimit (0.0, 1.0, s - margin);
            state.viewEnd01 = juce::jlimit (state.viewStart01 + 1.0e-6, 1.0, e + margin);
            waveArea->repaint();
            minimap->repaint();
        };
        addAndMakeVisible (zoomSelButton);

        waveArea = std::make_unique<WaveArea> (state);
        waveArea->onChanged = [this] { minimap->repaint(); updateReadouts(); };
        addAndMakeVisible (waveArea.get());

        minimap = std::make_unique<Minimap> (state);
        minimap->onChanged = [this] { waveArea->repaint(); updateReadouts(); };
        addAndMakeVisible (minimap.get());

        updateReadouts();
        setVisible (false);
        startTimerHz (30);
    }

    ~SampleEditor() override { stopTimer(); }

    // Set by the owner when this editor is hosted in its own window: CLOSE and Escape
    // must close that window rather than just hiding this component.
    std::function<void()> onCloseRequest;

    // Opens the overlay: shows it, brings it to front, grabs keyboard focus for
    // Escape-to-close, and (re)builds the peak pyramid if the sample changed since
    // it was last built.
    void openEditor()
    {
        auto name = processor.getDisplayName();
        auto n = processor.getDisplayBuffer().size();
        if (name != lastBuiltName || n != lastBuiltSize)
        {
            state.viewStart01 = 0.0;
            state.viewEnd01 = 1.0;
            triggerPyramidBuild();
        }
        updateReadouts();
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // rack panel chrome — the pop-out reuses the same face + border as every Block
        // on the main panel, not a flat fill.
        auto b = getLocalBounds().toFloat();
        g.setGradientFill (gradients::panel (b));
        g.fillRect (b);
        g.setColour (colour::panelBorder);
        g.drawRect (b, 1.0f);
    }

    void resized() override { layout(); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            closeEditor();
            return true;
        }
        return false;
    }

    // JUCE calls this on every component in the tree once the real LnF is installed;
    // the constructor may run before that, so this is what actually lands the LCD font.
    void lookAndFeelChanged() override { applyReadoutFont(); }

private:
    void closeEditor()
    {
        if (onCloseRequest) onCloseRequest();   // living in a pop-out window
        else                setVisible (false); // legacy in-panel overlay
    }

    // The cast can fail briefly before the owner installs TsLookAndFeel on the component
    // tree — fall back to a plain font rather than crash on a null LnF.
    void applyReadoutFont()
    {
        if (auto* lnf = dynamic_cast<TsLookAndFeel*> (&getLookAndFeel()))
            readoutLabel.setFont (lnf->lcdFont (14.0f));
        else
            readoutLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    }

    void layout()
    {
        auto area = getLocalBounds().reduced (8);

        auto topBar = area.removeFromTop (28);
        closeButton.setBounds (topBar.removeFromRight (64));
        topBar.removeFromRight (8);
        fitButton.setBounds (topBar.removeFromRight (48));
        topBar.removeFromRight (12);
        nameLabel.setBounds (topBar.removeFromLeft (juce::jmin (260, topBar.getWidth() / 2)));
        topBar.removeFromLeft (10);
        readoutLabel.setBounds (topBar);

        area.removeFromTop (8);

        // 68 (was 56): tall enough for the XFADE knob's title + dial + ms readout.
        auto bottomBar = area.removeFromBottom (68);
        area.removeFromBottom (8);

        auto minimapArea = area.removeFromBottom (24);
        area.removeFromBottom (6);

        waveArea->setBounds (area);
        minimap->setBounds (minimapArea);

        loopToggle->setBounds (bottomBar.removeFromLeft (56).withSizeKeepingCentre (52, 24));
        bottomBar.removeFromLeft (10);
        styleCombo->setBounds (bottomBar.removeFromLeft (96).withSizeKeepingCentre (92, 34));
        bottomBar.removeFromLeft (10);
        revToggle->setBounds (bottomBar.removeFromLeft (50).withSizeKeepingCentre (46, 24));
        bottomBar.removeFromLeft (14);
        xfadeKnob->setBounds (bottomBar.removeFromLeft (56).withSizeKeepingCentre (56, 68));
        bottomBar.removeFromLeft (14);
        zoomSelButton.setBounds (bottomBar.removeFromLeft (78).withSizeKeepingCentre (78, 24));
        bottomBar.removeFromLeft (10);
        snapButton.setBounds (bottomBar.removeFromLeft (60).withSizeKeepingCentre (60, 24));
        bottomBar.removeFromLeft (10);
        clearButton.setBounds (bottomBar.removeFromLeft (60).withSizeKeepingCentre (60, 24));
    }

    void triggerPyramidBuild()
    {
        lastBuiltName = processor.getDisplayName();
        lastBuiltSize = processor.getDisplayBuffer().size();
        state.pyramid.reset();
        builder.start (processor.getDisplayBuffer());
        if (builder.isReady())
            state.pyramid = builder.takeResult();
        if (waveArea != nullptr) waveArea->repaint();
        if (minimap != nullptr) minimap->repaint();
    }

    static juce::String groupDigits (juce::int64 v)
    {
        juce::String raw (v);
        juce::String out;
        int len = raw.length();
        for (int i = 0; i < len; ++i)
        {
            if (i > 0 && (len - i) % 3 == 0)
                out += ",";
            out += raw.substring (i, i + 1);
        }
        return out;
    }

    void updateReadouts()
    {
        nameLabel.setText (processor.getDisplayName(), juce::dontSendNotification);
        nameLabel.setColour (juce::Label::textColourId,
            processor.isSampleMissing() ? colour::warn : colour::silkLabel);

        const auto& buf = processor.getDisplayBuffer();
        auto* rs = processor.apvts.getParameter ("sample.regstart");
        auto* re = processor.apvts.getParameter ("sample.regend");
        if (rs == nullptr || re == nullptr || buf.empty())
        {
            readoutLabel.setText ({}, juce::dontSendNotification);
            return;
        }

        double sr = juce::jmax (1.0, processor.getDisplaySr()); // times in the file's own timeline
        double sNorm = (double) regGet (*rs), eNorm = (double) regGet (*re);
        double n = (double) buf.size();
        juce::int64 startSmp = (juce::int64) juce::jlimit (0.0, n, sNorm * n);
        juce::int64 endSmp   = (juce::int64) juce::jlimit (0.0, n, eNorm * n);
        juce::int64 lenSmp   = juce::jmax ((juce::int64) 0, endSmp - startSmp);

        auto secs = [sr] (juce::int64 smp) { return (double) smp / sr; };

        juce::String text;
        text << "START " << juce::String (secs (startSmp), 3) << " s | "
             << "END "   << juce::String (secs (endSmp), 3)   << " s | "
             << "LEN "   << juce::String (secs (lenSmp), 3)   << " s ("
             << groupDigits (lenSmp) << " smp)";
        readoutLabel.setText (text, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        processor.refreshDisplaySource();   // Tape mode draws the tape take

        if (! isVisible())
            return;

        if (builder.isReady())
        {
            state.pyramid = builder.takeResult();
            waveArea->repaint();
            minimap->repaint();
        }

        auto name = processor.getDisplayName();
        auto n = processor.getDisplayBuffer().size();
        if (name != lastBuiltName || n != lastBuiltSize)
            triggerPyramidBuild();

        updateReadouts();
        waveArea->repaint(); // playhead moves continuously during playback

        // STYLE and XFADE only matter while looping; REV is never greyed (works on
        // one-shots too). Piggybacks on this same 30 Hz timer per the existing pattern.
        if (auto* loopOnP = processor.apvts.getRawParameterValue ("sample.loopon"))
        {
            bool loopOn = loopOnP->load() > 0.5f;
            styleCombo->setActive (loopOn);
            xfadeKnob->setActive (loopOn);
        }
    }

    TurboSynthProcessor& processor;
    EditorState state;
    PyramidBuilder builder;
    juce::String lastBuiltName;
    size_t lastBuiltSize = 0;

    juce::Label nameLabel, readoutLabel;
    juce::TextButton closeButton { "CLOSE" };
    juce::TextButton fitButton { "FIT" };
    juce::TextButton snapButton { "SNAP" };
    juce::TextButton clearButton { "CLEAR" };
    juce::TextButton zoomSelButton { "ZOOM SEL" };
    std::unique_ptr<TextToggle> loopToggle;
    std::unique_ptr<Combo> styleCombo;
    std::unique_ptr<TextToggle> revToggle;
    std::unique_ptr<Knob> xfadeKnob;
    std::unique_ptr<WaveArea> waveArea;
    std::unique_ptr<Minimap> minimap;
};

// The editor used to cover the entire panel, which hid the very knobs you reach for
// while auditioning a region. It now lives in its own resizable window (docs/PANEL.md).
// The window does NOT own the SampleEditor - the view does - so zoom and scroll survive
// close/reopen. Always-on-top because a plugin's utility window must not disappear
// behind the host.
class SampleEditorWindow : public juce::DocumentWindow
{
public:
    // takes a plain Component so the window can host the mode-switching SourceEditorPanel
    SampleEditorWindow (juce::Component& content, juce::Component* anchor)
        : juce::DocumentWindow ("Sample Editor", colour::panelBot,
                                juce::DocumentWindow::closeButton, true)
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&content, false);
        setResizable (true, false);
        setResizeLimits (640, 340, 4000, 2400);
        centreAroundComponent (anchor, 940, 500);
        setAlwaysOnTop (true);
    }

    // Hide, never delete: deleting from inside a button callback would destroy the
    // component that is still handling the click.
    void closeButtonPressed() override { setVisible (false); }

private:
    // the editor's TooltipWindow is parented to the editor and cannot render here
    juce::TooltipWindow tips { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleEditorWindow)
};

} // namespace ts::ui
