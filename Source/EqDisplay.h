#pragma once
#include <JuceHeader.h>
#include "Style.h"
#include "PluginProcessor.h"
#include <functional>

namespace pepel
{
// Экран в духе Pro-Q: спектр до/после, кривая EQ и точки, которые пользователь создаёт сам.
//  - двойной клик по пустому месту: новая точка
//  - клик по точке: меню (колокол / режектор, динамическая, прослушать, удалить)
//  - тянуть точку: частота (X) и усиление (Y); колесо: ширина; Shift+колесо: порог динамики
//  - двойной клик по точке: удалить
//  - квадраты внизу: срез низа (HP) и верха (LP), тянуть по X, двойной клик: вкл/выкл
class EqDisplay : public juce::Component
{
public:
    enum class Type { None, Point, HP, LP };
    struct Sel { Type type = Type::None; int idx = 0; };

    std::function<void(Sel)> onSelect;
    Sel selected;

    explicit EqDisplay(PepelEQAudioProcessor& p) : proc(p)
    {
        for (auto& v : specIn) v = -120.0f;
        for (auto& v : specOut) v = -120.0f;
    }

    static int listenCodeOf(Sel s)
    {
        return s.type == Type::Point ? s.idx : (s.type == Type::HP ? kListenHP : (s.type == Type::LP ? kListenLP : -1));
    }

    void tick()
    {
        ++frame;
        for (int i = 0; i < Analyzer::numSpec; ++i)
        {
            const float a = proc.inAnalyzer.spec[(size_t) i].load();
            const float b = proc.outAnalyzer.spec[(size_t) i].load();
            specIn[i]  += (a - specIn[i])  * (a > specIn[i]  ? 0.6f : 0.18f);
            specOut[i] += (b - specOut[i]) * (b > specOut[i] ? 0.6f : 0.18f);
        }
        repaint();
    }

    void deleteSelected()
    {
        if (selected.type == Type::Point) removePoint(selected.idx);
        else if (selected.type == Type::HP) setBool(ids::hpOn, false);
        else if (selected.type == Type::LP) setBool(ids::lpOn, false);
        if (selected.type == Type::Point) { selected = {}; if (onSelect) onSelect(selected); }
    }

    // ---------------- рисование ----------------
    void paint(juce::Graphics& g) override
    {
        drawPanelFrame(g, getLocalBounds(), col::frameViolet);
        const auto pl = plot();
        g.setColour(hex(0x090A3C));
        g.fillRect(pl);

        const float fm[] = { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000,
                             2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 };
        for (float f : fm)
        {
            const bool major = (f == 100 || f == 1000 || f == 10000);
            g.setColour((major ? col::indigo : col::night).withAlpha(major ? 1.0f : 0.8f));
            g.fillRect(xOf(f), pl.getY(), 1, pl.getHeight());
        }
        for (int db = -24; db <= 24; db += 6)
        {
            g.setColour(db == 0 ? col::mist : col::indigo);
            g.fillRect(pl.getX(), yOfDb((float) db), pl.getWidth(), db == 0 ? 2 : 1);
            if (db % 12 == 0)
            {
                g.setColour(col::mist);
                g.setFont(Fonts::get().vt323(18.0f));
                g.drawText((db > 0 ? "+" : "") + juce::String(db), 4, yOfDb((float) db) - 8, 42, 16, juce::Justification::centredRight, false);
            }
        }
        g.setColour(col::mist);
        g.setFont(Fonts::get().vt323(18.0f));
        const std::pair<float, const char*> fl[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                     { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
        for (auto& p : fl)
            g.drawText(p.second, xOf(p.first) - 18, pl.getBottom() + 2, 36, 16, juce::Justification::centred, false);

        g.saveState();
        g.reduceClipRegion(pl);

        // спектр
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

        // подсветка прослушиваемого
        drawListenOverlay(g);

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

        drawNodes(g);

        g.setColour(col::textSoft);
        g.setFont(Fonts::get().vt323(22.0f));
        g.drawText(readout(), pl.getX() + 8, pl.getY() + 4, 560, 22, juce::Justification::centredLeft, false);

        const int lc = proc.listen.load();
        if (lc >= 0 && ((frame / 4) % 2 == 0))
        {
            g.setColour(col::blood);
            g.fillRect(pl.getRight() - 150, pl.getY() + 8, 10, 10);
            g.setColour(col::moon);
            g.setFont(Fonts::get().vt323(22.0f));
            g.drawText("LISTEN", pl.getRight() - 134, pl.getY() + 3, 130, 22, juce::Justification::centredLeft, false);
        }
        if (!anyPoint())
        {
            g.setColour(col::glow.withAlpha(0.8f));
            g.setFont(Fonts::get().press(10.0f));
            g.drawText(RU("ДВОЙНОЙ КЛИК: СОЗДАТЬ ТОЧКУ"), pl, juce::Justification::centred, false);
        }
    }

    // ---------------- мышь ----------------
    void mouseDown(const juce::MouseEvent& e) override
    {
        grabbed = false; moved = false;
        const auto h = hitNode(e.getPosition(), 14);
        if (h.type == Type::None)
        {
            if (e.getNumberOfClicks() >= 2 && plot().contains(e.getPosition())) addPointAt(e.getPosition());
            return;
        }
        selected = h;
        if (onSelect) onSelect(selected);

        if (e.getNumberOfClicks() >= 2)
        {
            deleteSelectedNode(h);
            return;
        }
        touched = paramsFor(h);
        for (auto* p : touched) p->beginChangeGesture();
        grabbed = true;
        downPos = e.getPosition();
        const auto pos = nodePos(h);
        grabDx = pos.x - e.x; grabDy = pos.y - e.y;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!grabbed) return;
        if (!moved && e.getPosition().getDistanceFrom(downPos) < 4) return;
        moved = true;
        const int x = e.x + grabDx, y = e.y + grabDy;
        const float f = juce::jlimit(20.0f, 20000.0f, fOf((float) x));
        const float db = dbOfY(y);
        switch (selected.type)
        {
            case Type::Point:
            {
                const bool notch = boolP(ids::pNotch(selected.idx));
                setReal(ids::pF(selected.idx), f);
                setReal(ids::pG(selected.idx), juce::jlimit(-24.0f, notch ? 0.0f : 24.0f, db));
                break;
            }
            case Type::HP: setReal(ids::hpF, f); setReal(ids::hpOn, 1.0f); break;
            case Type::LP: setReal(ids::lpF, f); setReal(ids::lpOn, 1.0f); break;
            default: break;
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const bool wasClick = grabbed && !moved;
        if (grabbed) for (auto* p : touched) p->endChangeGesture();
        grabbed = false; touched.clear();
        if (wasClick && e.getNumberOfClicks() < 2) showMenu();
    }

    void mouseMove(const juce::MouseEvent& e) override { hover = hitNode(e.getPosition(), 14); }
    void mouseExit(const juce::MouseEvent&) override { hover = {}; }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto h = hitNode(e.getPosition(), 18);
        if (h.type != Type::Point) return;
        const float d = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
        if (d == 0.0f) return;
        if (e.mods.isShiftDown())
        {
            if (!boolP(ids::pDyn(h.idx))) return;
            auto* p = proc.apvts.getParameter(ids::pThr(h.idx));
            const float cur = p->convertFrom0to1(p->getValue());
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(-70.0f, 0.0f, cur + (d > 0 ? 1.0f : -1.0f))));
            p->endChangeGesture();
        }
        else
        {
            auto* p = proc.apvts.getParameter(ids::pQ(h.idx));
            const float cur = p->convertFrom0to1(p->getValue());
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(0.3f, 40.0f, cur * (d > 0 ? 0.88f : 1.14f))));
            p->endChangeGesture();
        }
        selected = h;
        if (onSelect) onSelect(selected);
    }

    // команда из панели «ТОЧКА» (те же пункты, что в меню)
    void command(int id)
    {
        if (id == 5) { deleteSelected(); return; }
        applyMenu(selected, id);
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
        return pl.getY() + (int) ((24.0f - juce::jlimit(-24.0f, 24.0f, db)) / 48.0f * (float) pl.getHeight());
    }
    float dbOfY(int y) const
    {
        const auto pl = plot();
        return 24.0f - ((float) (y - pl.getY()) / (float) pl.getHeight()) * 48.0f;
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
    void setBool(const juce::String& id, bool on)
    {
        auto* p = proc.apvts.getParameter(id);
        p->beginChangeGesture(); p->setValueNotifyingHost(on ? 1.0f : 0.0f); p->endChangeGesture();
    }
    void putReal(const juce::String& id, float v)
    {
        auto* p = proc.apvts.getParameter(id);
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
    }
    bool anyPoint() const
    {
        for (int i = 0; i < kPts; ++i) if (boolP(ids::pOn(i))) return true;
        return boolP(ids::hpOn) || boolP(ids::lpOn);
    }

    std::vector<juce::RangedAudioParameter*> paramsFor(Sel s) const
    {
        auto g = [this](const juce::String& id) { return proc.apvts.getParameter(id); };
        switch (s.type)
        {
            case Type::Point: return { g(ids::pF(s.idx)), g(ids::pG(s.idx)) };
            case Type::HP:    return { g(ids::hpF), g(ids::hpOn) };
            case Type::LP:    return { g(ids::lpF), g(ids::lpOn) };
            default: return {};
        }
    }

    juce::Point<int> nodePos(Sel s) const
    {
        const auto pl = plot();
        switch (s.type)
        {
            case Type::Point: return { xOf(real(ids::pF(s.idx))), yOfDb(real(ids::pG(s.idx))) };
            case Type::HP:    return { xOf(real(ids::hpF)), pl.getBottom() - 14 };
            case Type::LP:    return { xOf(real(ids::lpF)), pl.getBottom() - 38 };
            default: return {};
        }
    }

    Sel hitNode(juce::Point<int> m, int radius) const
    {
        Sel best; int bestD = radius * radius + 1;
        auto test = [&](Type t, int n) {
            for (int i = 0; i < n; ++i)
            {
                Sel s { t, i };
                if (t == Type::Point && !boolP(ids::pOn(i))) continue;
                const auto p = nodePos(s);
                const int d = (p.x - m.x) * (p.x - m.x) + (p.y - m.y) * (p.y - m.y);
                if (d < bestD) { bestD = d; best = s; }
            }
        };
        test(Type::Point, kPts); test(Type::HP, 1); test(Type::LP, 1);
        return best;
    }

    void removePoint(int i)
    {
        setBool(ids::pOn(i), false);
        if (proc.listen.load() == i) proc.listen.store(-1);
    }

    void deleteSelectedNode(Sel h)
    {
        if (h.type == Type::Point) { removePoint(h.idx); selected = {}; if (onSelect) onSelect(selected); }
        else if (h.type == Type::HP) { setBool(ids::hpOn, !boolP(ids::hpOn)); }
        else if (h.type == Type::LP) { setBool(ids::lpOn, !boolP(ids::lpOn)); }
    }

    void addPointAt(juce::Point<int> m)
    {
        for (int j = 0; j < kPts; ++j)
            if (!boolP(ids::pOn(j)))
            {
                putReal(ids::pF(j), juce::jlimit(20.0f, 20000.0f, fOf((float) m.x)));
                putReal(ids::pG(j), juce::jlimit(-24.0f, 24.0f, dbOfY(m.y)));
                putReal(ids::pQ(j), 1.4f);
                putReal(ids::pThr(j), -30.0f);
                setBool(ids::pNotch(j), false);
                setBool(ids::pDyn(j), false);
                setBool(ids::pOn(j), true);
                selected = { Type::Point, j };
                if (onSelect) onSelect(selected);
                return;
            }
    }

    void showMenu()
    {
        const Sel s = selected;
        if (s.type == Type::None) return;
        juce::PopupMenu m;
        if (s.type == Type::Point)
        {
            m.addItem(1, RU("Колокол"), true, !boolP(ids::pNotch(s.idx)));
            m.addItem(2, RU("Режектор (только срез)"), true, boolP(ids::pNotch(s.idx)));
            m.addSeparator();
            m.addItem(3, RU("Динамическая"), true, boolP(ids::pDyn(s.idx)));
            m.addItem(4, RU("Прослушать эту точку"), true, proc.listen.load() == s.idx);
            m.addSeparator();
            m.addItem(5, RU("Удалить"));
        }
        else
        {
            const bool on = s.type == Type::HP ? boolP(ids::hpOn) : boolP(ids::lpOn);
            m.addItem(1, on ? RU("Выключить срез") : RU("Включить срез"));
            m.addItem(4, RU("Прослушать то, что срезается"), on, proc.listen.load() == listenCodeOf(s));
        }
        juce::Component::SafePointer<EqDisplay> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options(), [safe, s](int r) {
            if (safe == nullptr || r == 0) return;
            safe->applyMenu(s, r);
        });
    }

    void applyMenu(Sel s, int r)
    {
        if (s.type == Type::Point)
        {
            const int i = s.idx;
            if (r == 1 || r == 2)
            {
                const bool notch = (r == 2);
                setBool(ids::pNotch(i), notch);
                if (notch)
                {
                    if (real(ids::pG(i)) > -3.0f) putReal(ids::pG(i), -18.0f);
                    if (real(ids::pQ(i)) < 4.0f) putReal(ids::pQ(i), 10.0f);
                }
                else if (real(ids::pQ(i)) > 6.0f) putReal(ids::pQ(i), 1.4f);
            }
            else if (r == 3)
            {
                const bool on = !boolP(ids::pDyn(i));
                setBool(ids::pDyn(i), on);
                if (on && std::abs(real(ids::pG(i))) < 2.0f) putReal(ids::pG(i), -6.0f);
            }
            else if (r == 4) proc.listen.store(proc.listen.load() == i ? -1 : i);
            else if (r == 5) { removePoint(i); selected = {}; if (onSelect) onSelect(selected); }
        }
        else
        {
            const bool hp = s.type == Type::HP;
            if (r == 1) setBool(hp ? ids::hpOn : ids::lpOn, !boolP(hp ? ids::hpOn : ids::lpOn));
            else if (r == 4) proc.listen.store(proc.listen.load() == listenCodeOf(s) ? -1 : listenCodeOf(s));
        }
    }

    // ---------- кривая ----------
    static std::vector<float> stageQs(int slopeIdx)
    {
        static const float q[3][4] = { { 0.7071f, 0, 0, 0 }, { 0.5412f, 1.3066f, 0, 0 }, { 0.5098f, 0.6013f, 0.8999f, 2.5629f } };
        const int n = slopeIdx == 0 ? 1 : (slopeIdx == 1 ? 2 : 4);
        return std::vector<float>(q[slopeIdx], q[slopeIdx] + n);
    }

    void computeCurve(std::vector<float>& out) const
    {
        const auto pl = plot();
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
        const int n = pl.getWidth() / 3 + 1;
        out.assign((size_t) n, 0.0f);

        using Co = juce::dsp::IIR::Coefficients<float>;
        std::vector<Co::Ptr> cs;
        for (int i = 0; i < kPts; ++i)
        {
            if (!boolP(ids::pOn(i))) continue;
            float g = real(ids::pG(i));
            if (boolP(ids::pNotch(i))) g = juce::jmin(g, 0.0f);
            if (boolP(ids::pDyn(i))) g = proc.dynGainOut[i].load();
            if (std::abs(g) >= 0.01f)
                cs.push_back(Co::makePeakFilter(sr, real(ids::pF(i)), real(ids::pQ(i)), juce::Decibels::decibelsToGain(g)));
        }
        const auto qs = stageQs(juce::jlimit(0, 2, (int) std::lround(real(ids::slope))));
        if (boolP(ids::hpOn)) for (float q : qs) cs.push_back(Co::makeHighPass(sr, real(ids::hpF), q));
        if (boolP(ids::lpOn)) for (float q : qs) cs.push_back(Co::makeLowPass(sr, real(ids::lpF), q));

        for (int i = 0; i < n; ++i)
        {
            const double f = juce::jmin((double) fOf((float) (pl.getX() + i * 3 + 1)), sr * 0.49);
            double db = 0.0;
            for (auto& c : cs) db += 20.0 * std::log10(juce::jmax(1.0e-5, c->getMagnitudeForFrequency(f, sr)));
            out[(size_t) i] = (float) db;
        }
    }

    void drawListenOverlay(juce::Graphics& g) const
    {
        const int lc = proc.listen.load();
        if (lc < 0) return;
        const auto pl = plot();
        g.setColour(col::peach.withAlpha(0.28f));
        if (lc < kPts)
        {
            const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
            auto bp = juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, real(ids::pF(lc)), juce::jmax(0.5f, real(ids::pQ(lc))));
            for (int x = pl.getX(); x < pl.getRight(); x += 3)
            {
                const double f = juce::jmin((double) fOf((float) x + 1.0f), sr * 0.49);
                const double m = bp->getMagnitudeForFrequency(f, sr);
                const int h = (int) (juce::jmin(1.0, m * m * 1.6) * pl.getHeight());
                g.fillRect(x, pl.getBottom() - h, 3, h);
            }
        }
        else if (lc == kListenHP) g.fillRect(pl.getX(), pl.getY(), xOf(real(ids::hpF)) - pl.getX(), pl.getHeight());
        else if (lc == kListenLP) g.fillRect(xOf(real(ids::lpF)), pl.getY(), pl.getRight() - xOf(real(ids::lpF)), pl.getHeight());
    }

    // ---------- узлы ----------
    void drawNodes(juce::Graphics& g)
    {
        static const char* diamond[] = { "...#...", "..###..", ".#####.", "#######", ".#####.", "..###..", "...#..." };
        const int lc = proc.listen.load();
        for (int i = 0; i < kPts; ++i)
        {
            if (!boolP(ids::pOn(i))) continue;
            const bool sel = selected.type == Type::Point && selected.idx == i;
            const bool hov = hover.type == Type::Point && hover.idx == i;
            const bool notch = boolP(ids::pNotch(i)), dyn = boolP(ids::pDyn(i));
            const auto p = nodePos({ Type::Point, i });
            const int sc = (sel || hov) ? 3 : 2;
            const juce::Colour c = lc == i ? col::peach : (notch ? col::peach : col::blood);
            if (dyn)
            {
                g.setColour(col::glow);
                g.drawRect(p.x - 11, p.y - 11, 22, 22, 2);
            }
            if (notch)
            {
                drawSprite(g, diamond, 7, 7, p.x - 7 * sc / 2 + 1, p.y - 7 * sc / 2 + 1, sc, col::ink);
                drawSprite(g, diamond, 7, 7, p.x - 7 * sc / 2, p.y - 7 * sc / 2, sc, c);
            }
            else
            {
                drawSprite(g, sprites::heart, sprites::heartW, sprites::heartH, p.x - 7 * sc / 2 + 1, p.y - 3 * sc + 1, sc, col::ink);
                drawSprite(g, sprites::heart, sprites::heartW, sprites::heartH, p.x - 7 * sc / 2, p.y - 3 * sc, sc, c);
            }
            g.setColour(col::moon);
            g.setFont(Fonts::get().vt323(20.0f));
            g.drawText((dyn ? "D" : "") + juce::String(i + 1), p.x - 14, p.y - 30, 28, 16, juce::Justification::centred, false);
        }
        for (int k = 0; k < 2; ++k)
        {
            const Type t = k == 0 ? Type::HP : Type::LP;
            const bool on = boolP(k == 0 ? ids::hpOn : ids::lpOn);
            const bool sel = selected.type == t, hov = hover.type == t;
            const auto p = nodePos({ t, 0 });
            const int r = (sel || hov) ? 8 : 6;
            g.setColour(col::ink); g.fillRect(p.x - r - 2, p.y - r - 2, 2 * r + 4, 2 * r + 4);
            g.setColour(on ? col::glow : col::mist); g.fillRect(p.x - r, p.y - r, 2 * r, 2 * r);
            g.setColour(on ? col::blood : col::track); g.fillRect(p.x - 2, p.y - 2, 4, 4);
            g.setColour(on ? col::moon : col::mist);
            g.setFont(Fonts::get().vt323(18.0f));
            g.drawText(k == 0 ? "HP" : "LP", p.x - 16, p.y - r - 18, 32, 14, juce::Justification::centred, false);
        }
    }

    static juce::String fmtF(float f) { return f < 1000.0f ? juce::String(juce::roundToInt(f)) + "Hz" : juce::String(f / 1000.0f, 2) + "k"; }
    static juce::String fmtD(float d) { return (d >= 0.05f ? "+" : "") + juce::String(d, 1) + "dB"; }

    juce::String readout() const
    {
        const Sel s = (hover.type != Type::None) ? hover : selected;
        switch (s.type)
        {
            case Type::Point:
            {
                if (!boolP(ids::pOn(s.idx))) return {};
                juce::String t = (boolP(ids::pNotch(s.idx)) ? "NOTCH " : "BELL ") + juce::String(s.idx + 1) + "  " + fmtF(real(ids::pF(s.idx)))
                                 + "  " + fmtD(real(ids::pG(s.idx))) + "  Q " + juce::String(real(ids::pQ(s.idx)), 1);
                if (boolP(ids::pDyn(s.idx))) t += "  DYN THR " + juce::String(juce::roundToInt(real(ids::pThr(s.idx)))) + "dB";
                return t;
            }
            case Type::HP: return "LOW CUT " + fmtF(real(ids::hpF)) + (boolP(ids::hpOn) ? "  ON" : "  OFF");
            case Type::LP: return "HIGH CUT " + fmtF(real(ids::lpF)) + (boolP(ids::lpOn) ? "  ON" : "  OFF");
            default: return {};
        }
    }

    PepelEQAudioProcessor& proc;
    float specIn[Analyzer::numSpec], specOut[Analyzer::numSpec];
    Sel hover;
    bool grabbed = false, moved = false;
    int grabDx = 0, grabDy = 0, frame = 0;
    juce::Point<int> downPos;
    std::vector<juce::RangedAudioParameter*> touched;
};
} // namespace pepel
