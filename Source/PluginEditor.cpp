#include "PluginEditor.h"

using namespace pepel;

namespace
{
juce::String fmtDb(float v)   { return (v >= 0.05f ? "+" : "") + juce::String(v, 1) + "dB"; }
juce::String fmtThr(float v)  { return juce::String(juce::roundToInt(v)) + "dB"; }
juce::String fmtFreq(float v) { return v < 1000.0f ? juce::String(juce::roundToInt(v)) + "Hz" : juce::String(v / 1000.0f, 1) + "k"; }
juce::String fmtQ(float v)    { return juce::String(v, 1); }
juce::String fmtMs(float v)   { return juce::String(juce::roundToInt(v)) + "ms"; }
}

PepelEQEditor::PepelEQEditor(PepelEQAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p), weather(p)
{
    setOpaque(true);
    setSize(1190, 864);
    setResizable(false, false);

    background = makeBackground(1190, 864);
    scanlines = juce::Image(juce::Image::ARGB, 1190, 864, true);
    {
        juce::Graphics g(scanlines);
        g.setColour(col::ink.withAlpha(0.16f));
        for (int y = 0; y < 864; y += 3) g.fillRect(0, y, 1190, 1);
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

    // ---------- колонка 1 ----------
    auto* pg = addPanel(RU("ГРАФИЧЕСКИЙ"), col::frameBlue, cx[0], top, kGraphic);
    const char* names[kGraphic] = { "31", "63", "125", "250", "500", "1K", "2K", "4K", "8K", "16K" };
    for (int i = 0; i < kGraphic; ++i) addSlider(pg, names[i], ids::gain(i), fmtDb);

    auto* ps = addPanel(RU("САБ-СРЕЗ"), col::frameBlood, cx[0], top + Panel::heightFor(kGraphic) + 14, 2);
    addToggle(ps, RU("СРЕЗ"), ids::subOn, RU("ВКЛ"), RU("ВЫКЛ"));
    addSlider(ps, RU("ЧАСТОТА"), ids::subFreq, fmtFreq);

    auto* po = addPanel(RU("ВЫХОД"), col::frameViolet, cx[0], top + Panel::heightFor(kGraphic) + 14 + Panel::heightFor(2) + 14, 1);
    addSlider(po, RU("УРОВЕНЬ"), ids::outGain, fmtDb);

    // ---------- колонка 2: динамические точки ----------
    const juce::Colour cyc[4] = { col::frameCoral, col::frameBlue, col::frameBlood, col::frameViolet };
    for (int i = 0; i < kDyn; ++i)
    {
        auto* pd = addPanel(RU("ДИНАМИКА ") + juce::String(i + 1), cyc[i], cx[1], top + i * (Panel::heightFor(3) + 14), 3);
        addSlider(pd, RU("ЧАСТОТА"), ids::dFreq(i), fmtFreq);
        addSlider(pd, RU("ПОРОГ"), ids::dThr(i), fmtThr);
        addSlider(pd, RU("ЭФФЕКТ"), ids::dRng(i), fmtDb);
    }

    // ---------- колонка 3 ----------
    auto* pgl = addPanel(RU("ДЕТЕКТОР"), col::frameViolet, cx[2], top, 4);
    addToggle(pgl, RU("ВХОД"), ids::scOn, RU("СИДЧЕЙН"), RU("СИГНАЛ"));
    addSlider(pgl, RU("ДОБРОТН"), ids::dynQ, fmtQ);
    addSlider(pgl, RU("АТАКА"), ids::dynAtk, fmtMs);
    addSlider(pgl, RU("РЕЛИЗ"), ids::dynRel, fmtMs);

    auto* pf = addPanel(RU("ПОИСК ШУМА"), col::frameCoral, cx[2], top + Panel::heightFor(4) + 14, 5);
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

    auto* pn = addPanel(RU("РЕЖЕКТОРЫ"), col::frameBlood, cx[2], top + Panel::heightFor(4) + 14 + Panel::heightFor(5) + 14, 5);
    for (int i = 0; i < kNotch; ++i)
        addSlider(pn, RU("ФИЛЬТР ") + juce::String(i + 1), ids::nFreq(i), fmtFreq, ids::nOn(i));
    addSlider(pn, RU("ГЛУБИНА"), ids::nDepth, [](float v) { return juce::String(juce::roundToInt(v)) + "dB"; });

    addAndMakeVisible(weather);
    weather.setBounds(20, 864 - 96 - 12, 1150, 96);

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
}

void PepelEQEditor::paintOverChildren(juce::Graphics& g)
{
    // сканлайны
    g.drawImageAt(scanlines, 0, 0);

    // плёночное зерно
    for (int i = 0; i < 24; ++i)
    {
        g.setColour((i & 1 ? juce::Colours::white : col::ink).withAlpha(0.25f));
        g.fillRect(rng.nextInt(1190), rng.nextInt(864), 2, 1);
    }

    // редкие сбои
    if (glitchFrames > 0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.16f));
        g.fillRect(glitchBar);
        for (int i = 0; i < 4; ++i)
        {
            const int w = 60 + rng.nextInt(240), x = rng.nextInt(1190 - w);
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
        glitchBar = { 0, rng.nextInt(840), 1190, 10 + rng.nextInt(14) };
    }
    header.glitch = glitchFrames > 0;
    header.tick();
    weather.tick();
    for (auto& p : panels) p->syncAll();
    repaint();
}
