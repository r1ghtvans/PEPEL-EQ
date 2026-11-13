#pragma once
#include <JuceHeader.h>
#include "Style.h"
#include "PluginProcessor.h"
#include <functional>

namespace pepel
{
// Большой экран в духе Pro-Q: спектр до/после, кривая EQ, узлы, которые таскаются мышью.
//  - 10 узлов графического EQ: тянуть вверх/вниз (частота фиксирована), двойной клик: сброс
//  - 4 динамические точки: тянуть по X (частота) и Y (эффект), колесо: порог, двойной клик: убрать
//  - двойной клик по пустому месту: новая динамическая точка
//  - режекторы: тянуть по X (частота) и Y (глубина), двойной клик: выключить
//  - квадрат внизу: срез саба, тянуть по X, двойной клик: вкл/выкл
//  - треугольники сверху: найденный шум, клик ставит режектор
class EqDisplay : public juce::Component
{
public:
    enum class Type { None, Graphic, Dyn, Notch, Sub };
    struct Sel { Type type = Type::None; int idx = 0; };

    std::function<void(Sel)> onSelect;

    explicit EqDisplay(PepelEQAudioProcessor& p) : proc(p)
    {
        for (auto& v : specIn) v = -120.0f;
        for (auto& v : specOut) v = -120.0f;
    }

    Sel selected;

    void tick()
    {
        for (int i = 0; i < Analyzer::numSpec; ++i)
        {
            const float a = proc.inAnalyzer.spec[(size_t) i].load();
            const float b = proc.outAnalyzer.spec[(size_t) i].load();
            specIn[i]  += (a - specIn[i])  * (a > specIn[i]  ? 0.6f : 0.18f);
            specOut[i] += (b - specOut[i]) * (b > specOut[i] ? 0.6f : 0.18f);
        }
        repaint();
    }

    // ---------------- рисование ----------------
    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds();
        drawPanelFrame(g, b, col::frameViolet);
        const auto pl = plot();

        g.setColour(hex(0x090A3C));
        g.fillRect(pl);

        // сетка
        const float fm[] = { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000,
                             2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 };
        for (float f : fm)
        {
            const bool major = (f == 100 || f == 1000 || f == 10000);
            g.setColour((major ? col::indigo : col::night).withAlpha(major ? 1.0f : 0.8f));
            g.fillRect(xOf(f), pl.getY(), 1, pl.getHeight());
        }
        for (int db = -18; db <= 18; db += 6)
        {
            g.setColour(db == 0 ? col::mist : col::indigo);
            g.fillRect(pl.getX(), yOfDb((float) db), pl.getWidth(), db == 0 ? 2 : 1);
            g.setColour(col::mist);
            g.setFont(Fonts::get().vt323(18.0f));
            g.drawText((db > 0 ? "+" : "") + juce::String(db), 4, yOfDb((float) db) - 8, 42, 16, juce::Justification::centredRight, false);
        }
        g.setColour(col::mist);
        g.setFont(Fonts::get().vt323(18.0f));
        const std::pair<float, const char*> fl[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                     { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
        for (auto& p : fl)
            g.drawText(p.second, xOf(p.first) - 18, pl.getBottom() + 2, 36, 16, juce::Justification::centred, false);

        g.saveState();
        g.reduceClipRegion(pl);

        // спектр: до (заливка) и после (линия)
        for (int x = pl.getX(); x < pl.getRight(); x += 4)
        {
            const float f = fOf((float) x + 2.0f);
            const float pos = juce::jlimit(0.0f, (float) Analyzer::numSpec - 1.001f,
                                           std::log(f / 20.0f) / std::log(1000.0f) * (float) (Analyzer::numSpec - 1));
            const int i0 = (int) pos; const float fr = pos - (float) i0;
            const float tilt = 4.5f * std::log2(f / 1000.0f);
            auto lvl = [&](const float* a) {
                const float db = a[i0] * (1.0f - fr) + a[i0 + 1] * fr + tilt;
                return juce::jlimit(0.0f, 1.0f, (db + 100.0f) / 90.0f);
            };
            const int yIn  = pl.getBottom() - (int) (lvl(specIn)  * (float) pl.getHeight());
            const int yOut = pl.getBottom() - (int) (lvl(specOut) * (float) pl.getHeight());
            g.setColour(col::glow.withAlpha(0.16f));
            g.fillRect(x, yIn, 3, pl.getBottom() - yIn);
            g.setColour(col::starBlue.withAlpha(0.9f));
            g.fillRect(x, yIn, 3, 2);
            g.setColour(col::peach.withAlpha(0.85f));
            g.fillRect(x, yOut, 3, 2);
        }

        // кривая EQ
        std::vector<float> curve;
        computeCurve(curve);
        int prevY = yOfDb(0.0f);
        for (int i = 0; i < (int) curve.size(); ++i)
        {
            const int x = pl.getX() + i * 3;
            const int y = yOfDb(curve[(size_t) i]);
            const int y0 = yOfDb(0.0f);
            g.setColour((curve[(size_t) i] >= 0 ? col::coral : col::starBlue).withAlpha(0.38f));
            g.fillRect(x, juce::jmin(y, y0), 3, std::abs(y - y0));
            g.setColour(col::moon);
            g.fillRect(x, y - 1, 3, 2);
            if (i > 0) g.fillRect(x, juce::jmin(y, prevY), 2, std::abs(y - prevY) + 1);
            prevY = y;
        }
        g.restoreState();

        // найденный шум: треугольники сверху
        const int nf = proc.inAnalyzer.foundCount.load();
        for (int i = 0; i < nf; ++i)
        {
            const float f = proc.inAnalyzer.foundFreq[(size_t) i].load();
            const int x = xOf(f);
            g.setColour(col::peach);
            for (int r = 0; r < 4; ++r) g.fillRect(x - 6 + r * 2, pl.getY() + 2 + r * 3, 12 - r * 4, 3);
            g.setColour(col::ink);
            g.fillRect(x - 1, pl.getY() + 14, 2, 4);
        }

        // узлы
        drawNodes(g);

        // подсказка / значения
        g.setColour(col::textSoft);
        g.setFont(Fonts::get().vt323(22.0f));
        g.drawText(readout(), pl.getX() + 8, pl.getY() + 20, 520, 22, juce::Justification::centredLeft, false);
        g.setColour(col::mist);
        g.setFont(Fonts::get().press(8.0f));
        g.drawText(RU("2 КЛИКА НА ПУСТОМ МЕСТЕ: ДИН. ТОЧКА   КОЛЕСО НА ДИН. ТОЧКЕ: ПОРОГ"),
                   pl.getX() + 8, pl.getBottom() - 16, pl.getWidth() - 16, 12, juce::Justification::centredRight, false);
    }

    // ---------------- мышь ----------------
    void mouseDown(const juce::MouseEvent& e) override
    {
        grabbed = false;
        // найденный шум
        const auto pl = plot();
        const int nf = proc.inAnalyzer.foundCount.load();
        for (int i = 0; i < nf; ++i)
        {
            const int x = xOf(proc.inAnalyzer.foundFreq[(size_t) i].load());
            if (std::abs(e.x - x) < 9 && e.y < pl.getY() + 22 && hitNode(e.getPosition(), 12).type == Type::None)
            {
                proc.addNotchAt(proc.inAnalyzer.foundFreq[(size_t) i].load());
                return;
            }
        }
        const auto h = hitNode(e.getPosition(), 14);
        if (h.type == Type::None)
        {
            if (e.getNumberOfClicks() >= 2 && pl.contains(e.getPosition())) addDynAt(e.getPosition());
            return;
        }
        selected = h;
        if (onSelect) onSelect(selected);

        if (e.getNumberOfClicks() >= 2)
        {
            doubleClickNode(h);
            return;
        }
        touched = paramsFor(h);
        for (auto* p : touched) p->beginChangeGesture();
        grabbed = true;
        const auto pos = nodePos(h);
        grabDx = pos.x - e.x; grabDy = pos.y - e.y;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!grabbed) return;
        const int x = e.x + grabDx, y = e.y + grabDy;
        const float f = juce::jlimit(20.0f, 20000.0f, fOf((float) x));
        const float db = dbOfY(y);
        switch (selected.type)
        {
            case Type::Graphic: setReal(ids::gain(selected.idx), juce::jlimit(-12.0f, 12.0f, db)); break;
            case Type::Dyn:     setReal(ids::dFreq(selected.idx), juce::jlimit(40.0f, 16000.0f, f));
                                setReal(ids::dRng(selected.idx), juce::jlimit(-18.0f, 18.0f, std::abs(db) < 0.5f ? (db < 0 ? -0.5f : 0.5f) : db)); break;
            case Type::Notch:   setReal(ids::nFreq(selected.idx), f);
                                setReal(ids::nDepth, juce::jlimit(6.0f, 36.0f, -db)); break;
            case Type::Sub:     setReal(ids::subFreq, juce::jlimit(20.0f, 60.0f, f)); break;
            default: break;
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (grabbed) for (auto* p : touched) p->endChangeGesture();
        grabbed = false; touched.clear();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto h = hitNode(e.getPosition(), 14);
        if (h.type != hover.type || h.idx != hover.idx) { hover = h; }
    }
    void mouseExit(const juce::MouseEvent&) override { hover = {}; }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto h = hitNode(e.getPosition(), 18);
        if (h.type != Type::Dyn) return;
        auto* p = proc.apvts.getParameter(ids::dThr(h.idx));
        const float cur = p->convertFrom0to1(p->getValue());
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(-70.0f, 0.0f, cur + (w.deltaY > 0 ? 1.0f : -1.0f))));
        p->endChangeGesture();
        selected = h;
        if (onSelect) onSelect(selected);
    }

private:
    // ---------- геометрия ----------
    juce::Rectangle<int> plot() const { return { 50, 14, getWidth() - 50 - 18, getHeight() - 14 - 24 }; }
    int xOf(float f) const
    {
        const auto pl = plot();
        return pl.getX() + (int) (std::log(juce::jlimit(20.0f, 20000.0f, f) / 20.0f) / std::log(1000.0f) * (float) pl.getWidth());
    }
    float fOf(float x) const
    {
        const auto pl = plot();
        return 20.0f * std::pow(1000.0f, (x - (float) pl.getX()) / (float) pl.getWidth());
    }
    int yOfDb(float db) const
    {
        const auto pl = plot();
        return pl.getY() + (int) ((18.0f - juce::jlimit(-18.0f, 18.0f, db)) / 36.0f * (float) pl.getHeight());
    }
    float dbOfY(int y) const
    {
        const auto pl = plot();
        return 18.0f - ((float) (y - pl.getY()) / (float) pl.getHeight()) * 36.0f;
    }

    // ---------- параметры ----------
    float real(const juce::String& id) const
    {
        auto* p = proc.apvts.getParameter(id);
        return p->convertFrom0to1(p->getValue());
    }
    void setReal(const juce::String& id, float v)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(v));
    }
    bool boolP(const juce::String& id) const { return proc.apvts.getParameter(id)->getValue() > 0.5f; }

    std::vector<juce::RangedAudioParameter*> paramsFor(Sel s) const
    {
        auto g = [this](const juce::String& id) { return proc.apvts.getParameter(id); };
        switch (s.type)
        {
            case Type::Graphic: return { g(ids::gain(s.idx)) };
            case Type::Dyn:     return { g(ids::dFreq(s.idx)), g(ids::dRng(s.idx)) };
            case Type::Notch:   return { g(ids::nFreq(s.idx)), g(ids::nDepth) };
            case Type::Sub:     return { g(ids::subFreq) };
            default: return {};
        }
    }

    juce::Point<int> nodePos(Sel s) const
    {
        const auto pl = plot();
        switch (s.type)
        {
            case Type::Graphic: return { xOf(kGraphicFreqs[s.idx]), yOfDb(real(ids::gain(s.idx))) };
            case Type::Dyn:     return { xOf(real(ids::dFreq(s.idx))), yOfDb(real(ids::dRng(s.idx))) };
            case Type::Notch:   return { xOf(real(ids::nFreq(s.idx))), yOfDb(-real(ids::nDepth)) };
            case Type::Sub:     return { xOf(real(ids::subFreq)), pl.getBottom() - 10 };
            default: return {};
        }
    }
    bool nodeVisible(Sel s) const
    {
        switch (s.type)
        {
            case Type::Dyn:   return std::abs(real(ids::dRng(s.idx))) >= 0.05f;
            case Type::Notch: return boolP(ids::nOn(s.idx));
            default: return true;
        }
    }

    Sel hitNode(juce::Point<int> m, int radius) const
    {
        Sel best; int bestD = radius * radius + 1;
        auto test = [&](Type t, int n) {
            for (int i = 0; i < n; ++i)
            {
                Sel s { t, i };
                if (!nodeVisible(s)) continue;
                const auto p = nodePos(s);
                const int d = (p.x - m.x) * (p.x - m.x) + (p.y - m.y) * (p.y - m.y);
                if (d < bestD) { bestD = d; best = s; }
            }
        };
        test(Type::Dyn, kDyn); test(Type::Notch, kNotch); test(Type::Graphic, kGraphic); test(Type::Sub, 1);
        return best;
    }

    void doubleClickNode(Sel s)
    {
        auto reset = [this](const juce::String& id) {
            auto* p = proc.apvts.getParameter(id);
            p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture();
        };
        auto setBool = [this](const juce::String& id, bool on) {
            auto* p = proc.apvts.getParameter(id);
            p->beginChangeGesture(); p->setValueNotifyingHost(on ? 1.0f : 0.0f); p->endChangeGesture();
        };
        switch (s.type)
        {
            case Type::Graphic: reset(ids::gain(s.idx)); break;
            case Type::Dyn:     reset(ids::dRng(s.idx)); break;
            case Type::Notch:   setBool(ids::nOn(s.idx), false); break;
            case Type::Sub:     setBool(ids::subOn, !boolP(ids::subOn)); break;
            default: break;
        }
    }

    void addDynAt(juce::Point<int> m)
    {
        for (int j = 0; j < kDyn; ++j)
            if (std::abs(real(ids::dRng(j))) < 0.05f)
            {
                const float db = dbOfY(m.y);
                const float rng = std::abs(db) < 3.0f ? (db < 0 ? -4.0f : 4.0f) : juce::jlimit(-18.0f, 18.0f, db);
                auto put = [this](const juce::String& id, float v) {
                    auto* p = proc.apvts.getParameter(id);
                    p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
                };
                put(ids::dFreq(j), juce::jlimit(40.0f, 16000.0f, fOf((float) m.x)));
                put(ids::dThr(j), -30.0f);
                put(ids::dRng(j), rng);
                selected = { Type::Dyn, j };
                if (onSelect) onSelect(selected);
                return;
            }
    }

    // ---------- кривая ----------
    void computeCurve(std::vector<float>& out) const
    {
        const auto pl = plot();
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
        const int n = pl.getWidth() / 3 + 1;
        out.assign((size_t) n, 0.0f);

        using Co = juce::dsp::IIR::Coefficients<float>;
        std::vector<Co::Ptr> cs;
        for (int i = 0; i < kGraphic; ++i)
        {
            const float g = real(ids::gain(i));
            if (std::abs(g) >= 0.01f) cs.push_back(Co::makePeakFilter(sr, kGraphicFreqs[i], 1.4f, juce::Decibels::decibelsToGain(g)));
        }
        const float q = real(ids::dynQ);
        for (int j = 0; j < kDyn; ++j)
            if (std::abs(real(ids::dRng(j))) >= 0.05f)
            {
                const float g = proc.dynGainOut[j].load();
                if (std::abs(g) >= 0.01f) cs.push_back(Co::makePeakFilter(sr, real(ids::dFreq(j)), q, juce::Decibels::decibelsToGain(g)));
            }
        const float depth = real(ids::nDepth);
        for (int k = 0; k < kNotch; ++k)
            if (boolP(ids::nOn(k)))
                cs.push_back(Co::makePeakFilter(sr, real(ids::nFreq(k)), 12.0f, juce::Decibels::decibelsToGain(-depth)));
        if (boolP(ids::subOn))
        {
            cs.push_back(Co::makeHighPass(sr, real(ids::subFreq), 0.5412f));
            cs.push_back(Co::makeHighPass(sr, real(ids::subFreq), 1.3066f));
        }
        for (int i = 0; i < n; ++i)
        {
            const double f = juce::jmin((double) fOf((float) (pl.getX() + i * 3 + 1)), sr * 0.49);
            double db = 0.0;
            for (auto& c : cs) db += 20.0 * std::log10(juce::jmax(1.0e-5, c->getMagnitudeForFrequency(f, sr)));
            out[(size_t) i] = (float) db;
        }
    }

    // ---------- узлы ----------
    void drawNodes(juce::Graphics& g)
    {
        auto isSel = [this](Type t, int i) { return selected.type == t && selected.idx == i; };
        auto isHov = [this](Type t, int i) { return hover.type == t && hover.idx == i; };

        for (int i = 0; i < kGraphic; ++i)
        {
            const auto p = nodePos({ Type::Graphic, i });
            const int r = (isSel(Type::Graphic, i) || isHov(Type::Graphic, i)) ? 7 : 5;
            g.setColour(col::ink); g.fillRect(p.x - r - 2, p.y - r - 2, 2 * r + 4, 2 * r + 4);
            g.setColour(col::starBlue); g.fillRect(p.x - r, p.y - r, 2 * r, 2 * r);
            g.setColour(col::moon); g.fillRect(p.x - 2, p.y - 2, 4, 4);
        }
        for (int j = 0; j < kDyn; ++j)
        {
            if (!nodeVisible({ Type::Dyn, j })) continue;
            const auto p = nodePos({ Type::Dyn, j });
            const int sc = (isSel(Type::Dyn, j) || isHov(Type::Dyn, j)) ? 3 : 2;
            drawSprite(g, sprites::heart, sprites::heartW, sprites::heartH, p.x - 7 * sc / 2 + 1, p.y - 3 * sc + 2, sc, col::ink);
            drawSprite(g, sprites::heart, sprites::heartW, sprites::heartH, p.x - 7 * sc / 2, p.y - 3 * sc, sc, col::blood);
            g.setColour(col::moon);
            g.setFont(Fonts::get().vt323(20.0f));
            g.drawText(juce::String(j + 1), p.x - 10, p.y - 3 * sc - 18, 20, 16, juce::Justification::centred, false);
        }
        for (int k = 0; k < kNotch; ++k)
        {
            if (!nodeVisible({ Type::Notch, k })) continue;
            const auto p = nodePos({ Type::Notch, k });
            const int r = (isSel(Type::Notch, k) || isHov(Type::Notch, k)) ? 7 : 5;
            g.setColour(col::ink); g.fillRect(p.x - r - 2, p.y - r - 2, 2 * r + 4, 2 * r + 4);
            g.setColour(col::peach);
            g.fillRect(p.x - r, p.y - 2, 2 * r, 4); g.fillRect(p.x - 2, p.y - r, 4, 2 * r);
        }
        {
            const auto p = nodePos({ Type::Sub, 0 });
            const bool on = boolP(ids::subOn);
            const int r = (isSel(Type::Sub, 0) || isHov(Type::Sub, 0)) ? 7 : 5;
            g.setColour(col::ink); g.fillRect(p.x - r - 2, p.y - r - 2, 2 * r + 4, 2 * r + 4);
            g.setColour(on ? col::glow : col::mist); g.fillRect(p.x - r, p.y - r, 2 * r, 2 * r);
            g.setColour(on ? col::blood : col::track); g.fillRect(p.x - 2, p.y - 2, 4, 4);
        }
    }

    static juce::String fmtF(float f) { return f < 1000.0f ? juce::String(juce::roundToInt(f)) + "Hz" : juce::String(f / 1000.0f, 2) + "k"; }
    static juce::String fmtD(float d) { return (d >= 0.05f ? "+" : "") + juce::String(d, 1) + "dB"; }

    juce::String readout() const
    {
        const Sel s = (hover.type != Type::None) ? hover : selected;
        switch (s.type)
        {
            case Type::Graphic: return "BAND " + fmtF(kGraphicFreqs[s.idx]) + "  " + fmtD(real(ids::gain(s.idx)));
            case Type::Dyn:     return "DYN " + juce::String(s.idx + 1) + "  " + fmtF(real(ids::dFreq(s.idx))) + "  "
                                       + fmtD(real(ids::dRng(s.idx))) + "  THR " + juce::String(juce::roundToInt(real(ids::dThr(s.idx)))) + "dB";
            case Type::Notch:   return "NOTCH " + juce::String(s.idx + 1) + "  " + fmtF(real(ids::nFreq(s.idx))) + "  " + fmtD(-real(ids::nDepth));
            case Type::Sub:     return String_sub();
            default: return {};
        }
    }
    juce::String String_sub() const { return "SUB CUT " + fmtF(real(ids::subFreq)) + (boolP(ids::subOn) ? "  ON" : "  OFF"); }

    PepelEQAudioProcessor& proc;
    float specIn[Analyzer::numSpec], specOut[Analyzer::numSpec];
    Sel hover;
    bool grabbed = false;
    int grabDx = 0, grabDy = 0;
    std::vector<juce::RangedAudioParameter*> touched;
};
} // namespace pepel
