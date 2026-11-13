#pragma once
#include <JuceHeader.h>
#include "Style.h"
#include <functional>

namespace pepel
{
// Строка панели: сегментный слайдер / кнопка / строка найденной частоты. Высота 30 px.
class Row : public juce::Component
{
public:
    enum class Kind { Slider, Button, Found };
    static constexpr int nameW = 100, valueW = 80, rowH = 30;

    Row(Kind k, juce::String n) : kind(k), name(std::move(n))
    {
        setOpaque(true);
        setBufferedToImage(true);
        if (k != Kind::Slider) setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    // --- настройка ---
    juce::RangedAudioParameter* param = nullptr;       // Slider
    juce::RangedAudioParameter* toggleParam = nullptr; // Slider: клик по названию включает/выключает
    std::function<juce::String(float)> fmt;            // Slider: форматирование значения
    std::function<bool()> isOn;                        // Button
    std::function<void()> onClick;                     // Button, Found
    std::function<juce::String()> btnText, valueText, nameText; // Button/Found
    std::function<float()> barLevel;                   // Found: 0..1

    // перенацелить строку на другой параметр (панель выбранной точки)
    void setTarget(juce::RangedAudioParameter* p, const juce::String& n, std::function<juce::String(float)> f)
    {
        param = p; name = n; fmt = std::move(f);
        sNorm = -2.0f;
        setMouseCursor(p ? juce::MouseCursor::NormalCursor : juce::MouseCursor::NormalCursor);
        sync();
    }

    // опрос состояния (вызывается таймером 15 Гц)
    void sync()
    {
        float nrm = param ? param->getValue() : 0.0f;
        bool tog = toggleParam ? toggleParam->getValue() > 0.5f : true;
        bool on = isOn ? isOn() : false;
        juce::String v = currentValueText(), nm = currentName(), bt = btnText ? btnText() : juce::String();
        float lv = barLevel ? barLevel() : 0.0f;
        if (nrm != sNorm || tog != sTog || on != sOn || v != sVal || nm != sName || bt != sBtn || lv != sLevel)
        {
            sNorm = nrm; sTog = tog; sOn = on; sVal = v; sName = nm; sBtn = bt; sLevel = lv;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(col::paper);
        const int w = getWidth();
        const juce::Rectangle<int> nameR(0, 0, nameW, rowH), valR(w - valueW, 0, valueW, rowH);
        const int barX = nameW, barW = w - nameW - valueW;

        const bool dim = (kind == Kind::Slider && toggleParam && !sTog) || (kind == Kind::Found && sName.startsWith("-"));
        g.setColour(dim ? col::mist : col::text);
        drawMixed(g, sName, nameR.withTrimmedLeft(2), juce::Justification::centredLeft, 25.0f, 10.0f);

        if (kind == Kind::Button)
        {
            juce::Rectangle<int> b(barX, 4, 92, rowH - 8);
            g.setColour(col::ink);   g.fillRect(b);
            g.setColour(sOn ? col::blood : col::track); g.fillRect(b.reduced(2));
            g.setColour(sOn ? col::moon : col::glow);
            g.setFont(Fonts::get().press(10.0f));
            g.drawText(sBtn, b, juce::Justification::centred, false);
        }
        else
        {
            drawSegments(g, barX, barW);
        }

        g.setColour(col::textSoft);
        drawMixed(g, sVal, valR, juce::Justification::centredRight, 25.0f, 10.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (kind == Kind::Button || kind == Kind::Found)
        {
            if (onClick) onClick();
            return;
        }
        if (!param) return;
        if (toggleParam && e.x < nameW)
        {
            toggleParam->beginChangeGesture();
            toggleParam->setValueNotifyingHost(toggleParam->getValue() > 0.5f ? 0.0f : 1.0f);
            toggleParam->endChangeGesture();
            return;
        }
        param->beginChangeGesture();
        gesture = true;
        if (e.getNumberOfClicks() >= 2)   // двойной клик: сброс
        {
            param->setValueNotifyingHost(param->getDefaultValue());
            return;
        }
        setFromX(e.x);
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (gesture && param && e.getNumberOfClicks() < 2) setFromX(e.x); }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (gesture && param) param->endChangeGesture();
        gesture = false;
    }

private:
    juce::String currentValueText() const
    {
        if (kind == Kind::Slider)
            return (param && fmt) ? fmt(param->convertFrom0to1(param->getValue())) : juce::String();
        return valueText ? valueText() : juce::String();
    }
    juce::String currentName() const { return nameText ? nameText() : name; }

    int segCount(int barW) const { return juce::jmax(1, (barW + 2) / 8); }

    void drawSegments(juce::Graphics& g, int barX, int barW)
    {
        const int n = segCount(barW);
        const float prop = kind == Kind::Slider ? sNorm : sLevel;
        const int filled = juce::roundToInt(prop * (float) n);
        const bool off = (kind == Kind::Slider && toggleParam && !sTog);
        const int y = (rowH - 14) / 2;
        const float pos[3] = { 0.0f, 0.5f, 1.0f };
        const juce::Colour cs[3] = { col::blood, hex(0x7F6FE0), col::starBlue };
        for (int i = 0; i < n; ++i)
        {
            juce::Rectangle<int> s(barX + i * 8, y, 6, 14);
            if (i < filled && !(kind == Kind::Found && sLevel <= 0.0f))
            {
                juce::Colour c = keyframes(pos, cs, (float) i / (float) juce::jmax(1, n - 1));
                if (off) c = c.withMultipliedSaturation(0.2f).withMultipliedBrightness(0.5f);
                g.setColour(c);               g.fillRect(s);
                g.setColour(c.brighter(0.5f)); g.fillRect(s.withHeight(2));
            }
            else
            {
                g.setColour(col::track); g.fillRect(s);
            }
        }
        if (kind == Kind::Slider || sLevel > 0.0f)
        {
            const int idx = juce::jlimit(0, n - 1, filled);
            juce::Rectangle<int> s(barX + idx * 8, y, 6, 14);
            g.setColour(col::ink);  g.fillRect(s);
            g.setColour(off ? col::mist : col::moon); g.fillRect(s.reduced(2));
        }
    }

    void setFromX(int x)
    {
        const int barX = nameW, barW = getWidth() - nameW - valueW;
        const float span = (float) (segCount(barW) * 8 - 2);
        const float p = juce::jlimit(0.0f, 1.0f, ((float) x - (float) barX) / span);
        param->setValueNotifyingHost(p);
    }

    Kind kind;
    juce::String name;
    bool gesture = false;
    float sNorm = -1.0f, sLevel = -1.0f;
    bool sTog = true, sOn = false;
    juce::String sVal, sName, sBtn;
};

// Панель с рамкой и шапкой; строки лежат внутри
class Panel : public juce::Component
{
public:
    Panel(juce::String t, juce::Colour c) : title(std::move(t)), frame(c) {}

    Row* add(std::unique_ptr<Row> r)
    {
        auto* p = r.get();
        addAndMakeVisible(*p);
        rows.push_back(std::move(r));
        resized();
        return p;
    }
    static int heightFor(int nRows) { return 48 + 30 * nRows; }

    void resized() override
    {
        int y = 42;
        for (auto& r : rows) { r->setBounds(10, y, getWidth() - 20, Row::rowH); y += Row::rowH; }
    }

    void paint(juce::Graphics& g) override
    {
        drawPanelFrame(g, getLocalBounds(), frame);
        // шапка
        g.setColour(frame.withAlpha(0.45f));
        g.fillRect(8, 8, getWidth() - 16, 26);
        drawSprite(g, sprites::heart, sprites::heartW, sprites::heartH, 14, 15, 2, col::moon);
        g.setColour(col::text);
        g.setFont(Fonts::get().press(13.0f));
        g.drawText(title, juce::Rectangle<int>(36, 8, getWidth() - 44, 26), juce::Justification::centredLeft, false);
        g.setFont(Fonts::get().press(8.0f));
        for (size_t i = 0; i < lines.size(); ++i)
        {
            g.setColour(i % 2 == 0 ? col::text : col::glow);
            g.drawText(lines[i], juce::Rectangle<int>(14, 46 + (int) i * 22, getWidth() - 28, 16), juce::Justification::centredLeft, false);
        }
    }

    void setLines(std::vector<juce::String> l) { lines = std::move(l); repaint(); }
    void syncAll() { for (auto& r : rows) r->sync(); }

private:
    juce::String title;
    juce::Colour frame;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<juce::String> lines;
};
} // namespace pepel
