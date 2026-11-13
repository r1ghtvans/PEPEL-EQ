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

    // ---------- колонка 1 ----------
    auto* pgl = addPanel(RU("ДЕТЕКТОР"), col::frameViolet, cx[0], py, 4);
    addToggle(pgl, RU("ВХОД"), ids::scOn, RU("СИДЧЕЙН"), RU("СИГНАЛ"));
    addSlider(pgl, RU("ДОБРОТН"), ids::dynQ, fmtQ);
    addSlider(pgl, RU("АТАКА"), ids::dynAtk, fmtMs);
    addSlider(pgl, RU("РЕЛИЗ"), ids::dynRel, fmtMs);

    auto* ps = addPanel(RU("САБ-СРЕЗ"), col::frameBlood, cx[0], py + Panel::heightFor(4) + 14, 2);
    addToggle(ps, RU("СРЕЗ"), ids::subOn, RU("ВКЛ"), RU("ВЫКЛ"));
    addSlider(ps, RU("ЧАСТОТА"), ids::subFreq, fmtFreq);

    // ---------- колонка 2 ----------
    auto* pf = addPanel(RU("ПОИСК ШУМА"), col::frameCoral, cx[1], py, 5);
    {
        auto r = std::make_unique<Row>(Row::Kind::Button, RU("ШУМ"));
        r->isOn = [this] { return proc.inAnalyzer.searching.load(); };
        r->btnText = [this] { return proc.inAnalyzer.searching.load() ? RU("СТОП") : RU("ИСКАТЬ"); };
        r->valueText = [this] {
            return proc.inAnalyzer.searching.load() ? juce::String(proc.inAnalyzer.searchSeconds.load(), 0) + "s" : juce::String();
        };
        r->onClick = [this] {
            auto& a = proc.inAnalyzer;
            if (a.searching.load()) a.searching.store(false);
            else { a.clearRequest.store(true); a.searching.store(true); }
        };
        pf->add(std::move(r));
    }
    for (int i = 0; i < Analyzer::maxFound; ++i)
    {
        auto r = std::make_unique<Row>(Row::Kind::Found, juce::String());
        r->nameText = [this, i] {
            return i < proc.inAnalyzer.foundCount.load() ? RU("УБРАТЬ ") + juce::String(i + 1) : juce::String("---");
        };
        r->valueText = [this, i] {
            return i < proc.inAnalyzer.foundCount.load() ? fmtFreq(proc.inAnalyzer.foundFreq[(size_t) i].load()) : juce::String();
        };
        r->barLevel = [this, i] {
            return i < proc.inAnalyzer.foundCount.load()
                       ? juce::jlimit(0.05f, 1.0f, proc.inAnalyzer.foundProm[(size_t) i].load() / 30.0f) : 0.0f;
        };
        r->onClick = [this, i] {
            if (i < proc.inAnalyzer.foundCount.load())
                proc.addNotchAt(proc.inAnalyzer.foundFreq[(size_t) i].load());
        };
        pf->add(std::move(r));
    }

    auto* pt = addPanel(RU("ТОЧКА"), col::frameBlue, cx[1], py + Panel::heightFor(5) + 14, 3);
    for (int i = 0; i < 3; ++i)
    {
        auto r = std::make_unique<Row>(Row::Kind::Slider, juce::String("---"));
        pointRows[i] = pt->add(std::move(r));
    }

    // ---------- колонка 3 ----------
    auto* pn = addPanel(RU("РЕЖЕКТОРЫ"), col::frameBlood, cx[2], py, 5);
    for (int i = 0; i < kNotch; ++i)
        addSlider(pn, RU("ФИЛЬТР ") + juce::String(i + 1), ids::nFreq(i), fmtFreq, ids::nOn(i));
    addSlider(pn, RU("ГЛУБИНА"), ids::nDepth, [](float v) { return juce::String(juce::roundToInt(v)) + "dB"; });

    auto* po = addPanel(RU("ВЫХОД"), col::frameViolet, cx[2], py + Panel::heightFor(5) + 14, 1);
    addSlider(po, RU("УРОВЕНЬ"), ids::outGain, fmtDb);

    addAndMakeVisible(weather);
    weather.setBounds(20, kH - 96 - 30, 1150, 96);
    applySelection({ EqDisplay::Type::Graphic, 5 });

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
    juce::RangedAudioParameter* p0 = nullptr; juce::RangedAudioParameter* p1 = nullptr; juce::RangedAudioParameter* p2 = nullptr;
    juce::String n1 = RU("УРОВЕНЬ"); std::function<juce::String(float)> f1 = fmtDb;
    switch (s.type)
    {
        case T::Graphic: p1 = P(ids::gain(s.idx)); break;
        case T::Dyn:     p0 = P(ids::dFreq(s.idx)); p1 = P(ids::dRng(s.idx)); p2 = P(ids::dThr(s.idx)); n1 = RU("ЭФФЕКТ"); break;
        case T::Notch:   p0 = P(ids::nFreq(s.idx)); p1 = P(ids::nDepth); n1 = RU("ГЛУБИНА");
                         f1 = [](float v) { return juce::String(juce::roundToInt(v)) + "dB"; }; break;
        case T::Sub:     p0 = P(ids::subFreq); break;
        default: break;
    }
    pointRows[0]->setTarget(p0, RU("ЧАСТОТА"), fmtFreq);
    pointRows[1]->setTarget(p1, n1, f1);
    pointRows[2]->setTarget(p2, RU("ПОРОГ"), fmtThr);
}
