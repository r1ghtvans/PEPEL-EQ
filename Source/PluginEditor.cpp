#include "PluginEditor.h"

using namespace pepel;

static constexpr int kW = 1190, kH = 920;

namespace
{
juce::String fmtDb(float v)   { return (v >= 0.05f ? "+" : "") + juce::String(v, 1) + "dB"; }
juce::String fmtThr(float v)  { return juce::String(juce::roundToInt(v)) + "dB"; }
juce::String fmtFreq(float v) { return v < 1000.0f ? juce::String(juce::roundToInt(v)) + "Hz" : juce::String(v / 1000.0f, 1) + "k"; }
juce::String fmtQ(float v)    { return juce::String(v, 1); }
juce::String fmtMs(float v)   { return juce::String(juce::roundToInt(v)) + "ms"; }
}

PepelEQEditor::PepelEQEditor(PepelEQAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p), weather(p), display(p)
{
    setOpaque(true);
    setSize(kW, kH);
    setResizable(false, false);

    background = makeBackground(kW, kH);
    scanlines = juce::Image(juce::Image::ARGB, kW, kH, true);
    {
        juce::Graphics g(scanlines);
        g.setColour(col::ink.withAlpha(0.16f));
        for (int y = 0; y < kH; y += 3) g.fillRect(0, y, kW, 1);
    }

    addAndMakeVisible(header);
    header.setBounds(0, 0, 1190, HeaderView::H);
    header.presetText = [this] {
        return juce::String(proc.getCurrentProgram() + 1).paddedLeft('0', 2) + " " + proc.getProgramName(proc.getCurrentProgram());
    };
    header.onPreset = [this](int dir) {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram((proc.getCurrentProgram() + dir + n) % n);
    };

    const int cx[3] = { 20, 410, 800 };
    const int top = HeaderView::H + 14;

    // ---------- экран (как в Pro-Q) ----------
    addAndMakeVisible(display);
    display.setBounds(20, top, 1150, 280);
    display.onSelect = [this](EqDisplay::Sel s) { applySelection(s); };
    const int py = top + 280 + 14;   // панели ниже экрана

    // ---------- колонка 1: сайдчейн и выход ----------
    auto* psc = addPanel(RU("СИДЧЕЙН"), col::frameViolet, cx[0], py, 4);
    addToggle(psc, RU("ВХОД"), ids::scOn, RU("СИДЧЕЙН"), RU("СИГНАЛ"));
    {
        auto r = std::make_unique<Row>(Row::Kind::Found, RU("SC ВХОД"));
        r->valueText = [this] {
            if (!proc.scBusOn.load()) return RU("НЕТ");
            return proc.scLevel.load() > 0.003f ? RU("ЕСТЬ") : RU("ТИХО");
        };
        r->barLevel = [this] {
            const float db = 20.0f * std::log10(juce::jmax(proc.scLevel.load(), 1.0e-4f));
            return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        };
        psc->add(std::move(r));
    }
    addSlider(psc, RU("АТАКА"), ids::dynAtk, fmtMs);
    addSlider(psc, RU("РЕЛИЗ"), ids::dynRel, fmtMs);

    auto* po = addPanel(RU("ВЫХОД"), col::frameCoral, cx[0], py + Panel::heightFor(4) + 14, 1);
    addSlider(po, RU("УРОВЕНЬ"), ids::outGain, fmtDb);

    // ---------- колонка 2: выбранная точка ----------
    auto* pt = addPanel(RU("ТОЧКА"), col::frameBlue, cx[1], py, 8);
    for (int i = 0; i < 4; ++i)
        pointRows[i] = pt->add(std::make_unique<Row>(Row::Kind::Slider, juce::String("---")));

    auto selPoint = [this] { return display.selected.type == EqDisplay::Type::Point; };
    auto selBool = [this, selPoint](const char* kind) -> juce::RangedAudioParameter* {
        if (!selPoint()) return nullptr;
        const int i = display.selected.idx;
        return proc.apvts.getParameter(juce::String(kind) == "notch" ? ids::pNotch(i) : ids::pDyn(i));
    };
    addButton(pt, RU("ТИП"),
        [selBool] { auto* p = selBool("notch"); return p && p->getValue() > 0.5f; },
        [selBool] { auto* p = selBool("notch"); return (p && p->getValue() > 0.5f) ? RU("РЕЖЕКТОР") : RU("КОЛОКОЛ"); },
        [this, selBool] { auto* p = selBool("notch"); if (p) display.command(p->getValue() > 0.5f ? 1 : 2); });
    addButton(pt, RU("ДИНАМИКА"),
        [selBool] { auto* p = selBool("dyn"); return p && p->getValue() > 0.5f; },
        [selBool] { auto* p = selBool("dyn"); return (p && p->getValue() > 0.5f) ? RU("ВКЛ") : RU("ВЫКЛ"); },
        [this, selBool] { if (selBool("dyn")) display.command(3); });
    addButton(pt, RU("ПРОСЛУШКА"),
        [this] { const int c = EqDisplay::listenCodeOf(display.selected); return c >= 0 && proc.listen.load() == c; },
        [this] { const int c = EqDisplay::listenCodeOf(display.selected); return (c >= 0 && proc.listen.load() == c) ? RU("СТОП") : RU("СЛУШАТЬ"); },
        [this] { if (EqDisplay::listenCodeOf(display.selected) >= 0) display.command(4); });
    addButton(pt, RU("ТОЧКА"),
        [] { return false; },
        [] { return RU("УДАЛИТЬ"); },
        [this] { if (display.selected.type != EqDisplay::Type::None) display.command(5); });

    // ---------- колонка 3: срезы и подсказки ----------
    auto* pc = addPanel(RU("СРЕЗЫ"), col::frameBlood, cx[2], py, 3);
    addSlider(pc, RU("НИЗ"), ids::hpF, fmtFreq, ids::hpOn);
    addSlider(pc, RU("ВЕРХ"), ids::lpF, fmtFreq, ids::lpOn);
    addSlider(pc, RU("КРУТИЗНА"), ids::slope, [](float v) { const int i = juce::roundToInt(v); return juce::String(12 * (1 << juce::jlimit(0, 2, i))) + "dB"; });

    auto* ph = addPanel(RU("ПОДСКАЗКИ"), col::frameViolet, cx[2], py + Panel::heightFor(3) + 14, 4);
    ph->setLines({ RU("2 КЛИКА ПО ЭКРАНУ: НОВАЯ ТОЧКА"),
                   RU("КЛИК ПО ТОЧКЕ: МЕНЮ ТИПА"),
                   RU("КОЛЕСО: ШИРИНА  SHIFT: ПОРОГ"),
                   RU("2 КЛИКА ПО ТОЧКЕ: УДАЛИТЬ"),
                   RU("НИЗ/ВЕРХ: КВАДРАТЫ ВНИЗУ ЭКРАНА") });

    addAndMakeVisible(weather);
    weather.setBounds(20, kH - 96 - 30, 1150, 96);
    applySelection({});

    for (auto& pnl : panels) pnl->syncAll();
    startTimerHz(15);
}

PepelEQEditor::~PepelEQEditor() { stopTimer(); }

Panel* PepelEQEditor::addPanel(const juce::String& title, juce::Colour frame, int x, int y, int nRows)
{
    auto p = std::make_unique<Panel>(title, frame);
    p->setBounds(x, y, 370, Panel::heightFor(nRows));
    addAndMakeVisible(*p);
    panels.push_back(std::move(p));
    return panels.back().get();
}

Row* PepelEQEditor::addSlider(Panel* p, const juce::String& name, const juce::String& id,
                              std::function<juce::String(float)> fmt, const juce::String& toggleId)
{
    auto r = std::make_unique<Row>(Row::Kind::Slider, name);
    r->param = proc.apvts.getParameter(id);
    r->fmt = std::move(fmt);
    if (toggleId.isNotEmpty()) r->toggleParam = proc.apvts.getParameter(toggleId);
    return p->add(std::move(r));
}

Row* PepelEQEditor::addToggle(Panel* p, const juce::String& name, const juce::String& id,
                              const juce::String& onText, const juce::String& offText)
{
    auto r = std::make_unique<Row>(Row::Kind::Button, name);
    auto* prm = proc.apvts.getParameter(id);
    r->isOn = [prm] { return prm->getValue() > 0.5f; };
    r->btnText = [prm, onText, offText] { return prm->getValue() > 0.5f ? onText : offText; };
    r->onClick = [prm] {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->getValue() > 0.5f ? 0.0f : 1.0f);
        prm->endChangeGesture();
    };
    return p->add(std::move(r));
}

void PepelEQEditor::resized()
{
    for (auto& p : panels) p->resized();
}

void PepelEQEditor::paint(juce::Graphics& g)
{
    g.drawImageAt(background, 0, 0);
    for (auto& p : panels) drawPanelShadow(g, p->getBounds());
    drawPanelShadow(g, weather.getBounds());
    drawPanelShadow(g, display.getBounds());

    // подпись внизу справа
    const juce::Rectangle<int> sig(kW - 20 - 300, kH - 28, 300, 22);
    g.setFont(Fonts::get().press(13.0f));
    g.setColour(col::ink.withAlpha(0.8f));
    g.drawText(RU("by ПЕПЕL"), sig.translated(2, 2), juce::Justification::centredRight, false);
    g.setColour(col::cyan.withAlpha(0.7f));
    g.drawText(RU("by ПЕПЕL"), sig.translated(-1, 0), juce::Justification::centredRight, false);
    g.setColour(col::peach);
    g.drawText(RU("by ПЕПЕL"), sig, juce::Justification::centredRight, false);
}

void PepelEQEditor::paintOverChildren(juce::Graphics& g)
{
    // сканлайны
    g.drawImageAt(scanlines, 0, 0);

    // плёночное зерно
    for (int i = 0; i < 24; ++i)
    {
        g.setColour((i & 1 ? juce::Colours::white : col::ink).withAlpha(0.25f));
        g.fillRect(rng.nextInt(kW), rng.nextInt(kH), 2, 1);
    }

    // редкие сбои
    if (glitchFrames > 0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.16f));
        g.fillRect(glitchBar);
        for (int i = 0; i < 4; ++i)
        {
            const int w = 60 + rng.nextInt(240), x = rng.nextInt(kW - w);
            const int y = glitchBar.getY() + rng.nextInt(juce::jmax(1, glitchBar.getHeight() - 3));
            g.setColour((i & 1 ? col::cyan : col::pink).withAlpha(0.5f));
            g.fillRect(x, y, w, 3);
        }
    }
}

void PepelEQEditor::timerCallback()
{
    if (glitchFrames > 0) --glitchFrames;
    else if (--nextGlitch <= 0)
    {
        glitchFrames = 2 + rng.nextInt(3);
        nextGlitch = 45 + rng.nextInt(30);
        glitchBar = { 0, rng.nextInt(kH - 24), kW, 10 + rng.nextInt(14) };
    }
    header.glitch = glitchFrames > 0;
    header.tick();
    weather.tick();
    display.tick();
    for (auto& p : panels) p->syncAll();
    repaint();
}

void PepelEQEditor::applySelection(EqDisplay::Sel s)
{
    using T = EqDisplay::Type;
    auto P = [this](const juce::String& id) { return proc.apvts.getParameter(id); };
    juce::RangedAudioParameter *p0 = nullptr, *p1 = nullptr, *p2 = nullptr, *p3 = nullptr;
    switch (s.type)
    {
        case T::Point: p0 = P(ids::pF(s.idx)); p1 = P(ids::pG(s.idx)); p2 = P(ids::pQ(s.idx)); p3 = P(ids::pThr(s.idx)); break;
        case T::HP:    p0 = P(ids::hpF); break;
        case T::LP:    p0 = P(ids::lpF); break;
        default: break;
    }
    pointRows[0]->setTarget(p0, RU("ЧАСТОТА"), fmtFreq);
    pointRows[1]->setTarget(p1, RU("УСИЛЕНИЕ"), fmtDb);
    pointRows[2]->setTarget(p2, RU("ШИРИНА"), fmtQ);
    pointRows[3]->setTarget(p3, RU("ПОРОГ"), fmtThr);
}

Row* PepelEQEditor::addButton(Panel* p, const juce::String& name, std::function<bool()> on,
                              std::function<juce::String()> text, std::function<void()> click)
{
    auto r = std::make_unique<Row>(Row::Kind::Button, name);
    r->isOn = std::move(on);
    r->btnText = std::move(text);
    r->onClick = std::move(click);
    return p->add(std::move(r));
}
