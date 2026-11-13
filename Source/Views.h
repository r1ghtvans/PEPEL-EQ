#pragma once
#include <JuceHeader.h>
#include "Style.h"
#include "PluginProcessor.h"
#include <functional>
#include <vector>

namespace pepel
{
// ============================ ШАПКА (118 px) ============================
class HeaderView : public juce::Component
{
public:
    static constexpr int H = 118;
    std::function<void(int)> onPreset;      // -1 назад, +1 вперёд
    std::function<juce::String()> presetText;
    bool glitch = false;

    HeaderView()
    {
        juce::Random r(20240611);
        for (int i = 0; i < 46; ++i)
        {
            Star s;
            s.x = r.nextInt(1190); s.y = r.nextInt(104);
            s.sparkle = i < 10;
            s.period = 8 + r.nextInt(20); s.phase = r.nextInt(30);
            stars.push_back(s);
        }
        for (int i = 0; i < 7; ++i)
        {
            Cloud c;
            c.size = i % 3; c.x = r.nextFloat() * 1300.0f - 60.0f; c.y = 36 + r.nextInt(56);
            c.speed = 0.35f + r.nextFloat() * 0.9f;
            c.colour = i % 3 == 0 ? col::indigo : (i % 3 == 1 ? col::violetDk : col::mist);
            clouds.push_back(c);
        }
    }

    void tick()
    {
        ++frame;
        for (auto& c : clouds) { c.x += c.speed; if (c.x > 1250.0f) c.x = -120.0f; }
        juce::Random& r = juce::Random::getSystemRandom();
        if (!comet.active && r.nextFloat() < 0.015f)
        {
            comet.active = true; comet.x = 500.0f + r.nextFloat() * 700.0f; comet.y = -4.0f;
        }
        if (comet.active)
        {
            comet.x -= 16.0f; comet.y += 8.0f;
            if (comet.y > 112.0f || comet.x < -40.0f) comet.active = false;
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const int w = getWidth();
        for (int y = 0; y < 112; y += 8)
        {
            g.setColour(lerpC(hex(0x00003E), hex(0x2A2490), (float) y / 104.0f));
            g.fillRect(0, y, w, 8);
        }
        for (auto& s : stars)
        {
            const bool on = ((frame + s.phase) % s.period) < (s.period * 7 / 10);
            if (!s.sparkle)
            {
                g.setColour(col::starBlue.withAlpha(on ? 1.0f : 0.35f));
                g.fillRect(s.x, s.y, 2, 2);
            }
            else if (on)
            {
                g.setColour(col::moon);
                g.fillRect(s.x + 2, s.y, 1, 5); g.fillRect(s.x, s.y + 2, 5, 1);
                g.setColour(col::glow); g.fillRect(s.x + 1, s.y + 1, 3, 3);
                g.setColour(col::moon); g.fillRect(s.x + 2, s.y + 2, 1, 1);
            }
        }
        drawSprite(g, sprites::crescent, sprites::crescentW, sprites::crescentH, w - 140 + 3, 20 + 3, 4, col::ink.withAlpha(0.6f));
        drawSprite(g, sprites::crescent, sprites::crescentW, sprites::crescentH, w - 140, 20, 4, col::moon);
        for (auto& c : clouds)
        {
            const char* const* rows = c.size == 0 ? sprites::cloudS : (c.size == 1 ? sprites::cloudM : sprites::cloudL);
            const int cw = c.size == 0 ? sprites::cloudSW : (c.size == 1 ? sprites::cloudMW : sprites::cloudLW);
            const int ch = c.size == 0 ? sprites::cloudSH : (c.size == 1 ? sprites::cloudMH : sprites::cloudLH);
            drawSprite(g, rows, cw, ch, (int) c.x, c.y, 4, c.colour);
        }
        if (comet.active)
        {
            for (int i = 8; i >= 1; --i)
            {
                g.setColour(col::glow.withAlpha(0.6f * (1.0f - i / 9.0f)));
                g.fillRect((int) (comet.x + i * 8), (int) (comet.y - i * 4), 4, 4);
            }
            g.setColour(col::moon); g.fillRect((int) comet.x, (int) comet.y, 4, 4);
        }
        const int kx = w / 2 - 56, ky = 32;
        drawSprite(g, sprites::kanjiHai, 16, 16, kx + 3, ky + 3, 3, col::ink.withAlpha(0.8f));
        drawSprite(g, sprites::kanjiHai, 16, 16, kx, ky, 3, col::moon);
        drawSprite(g, sprites::kanjiAi, 16, 16, kx + 64 + 3, ky + 3, 3, col::ink.withAlpha(0.8f));
        drawSprite(g, sprites::kanjiAi, 16, 16, kx + 64, ky, 3, col::blood);

        const int off = glitch ? 4 : 2;
        const juce::Rectangle<int> tr(30, 14, 420, 36);
        g.setFont(Fonts::get().press(30.0f));
        g.setColour(col::ink.withAlpha(0.8f)); g.drawText("PEPEL EQ", tr.translated(3, 3), juce::Justification::centredLeft, false);
        g.setColour(col::cyan.withAlpha(0.85f)); g.drawText("PEPEL EQ", tr.translated(-off, 0), juce::Justification::centredLeft, false);
        g.setColour(col::red.withAlpha(0.85f));  g.drawText("PEPEL EQ", tr.translated(off, 0), juce::Justification::centredLeft, false);
        g.setColour(col::moon);                  g.drawText("PEPEL EQ", tr, juce::Justification::centredLeft, false);

        const auto pr = presetRect();
        g.setColour(col::ink.withAlpha(0.55f)); fillStepped(g, pr.translated(3, 3), 6);
        drawPanelFrame(g, pr, col::frameViolet);
        g.setColour(col::coral);
        g.setFont(Fonts::get().press(13.0f));
        g.drawText("<", pr.withWidth(34), juce::Justification::centred, false);
        g.drawText(">", pr.withTrimmedLeft(pr.getWidth() - 34), juce::Justification::centred, false);
        g.setColour(col::text);
        g.drawText(presetText ? presetText() : juce::String(), pr.withTrimmedLeft(34).withTrimmedRight(34),
                   juce::Justification::centred, false);

        g.setColour(col::blood); g.fillRect(0, 109, w, 3);
        g.setColour(col::ink);   g.fillRect(0, 112, w, 3);
        g.setColour(col::ink);   g.fillRect(0, 115, w, 3);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto pr = presetRect();
        if (pr.contains(e.getPosition()) && onPreset)
            onPreset(e.x < pr.getX() + 40 ? -1 : +1);
    }

private:
    juce::Rectangle<int> presetRect() const { return { 30, 62, 340, 36 }; }

    struct Star { int x, y; bool sparkle; int period, phase; };
    struct Cloud { int size; float x; int y; float speed; juce::Colour colour; };
    struct Comet { bool active = false; float x = 0, y = 0; };
    std::vector<Star> stars;
    std::vector<Cloud> clouds;
    Comet comet;
    int frame = 0;
};

// ============================ ПОГОДА + ОСЦИЛЛОГРАММА ============================
class WeatherView : public juce::Component
{
public:
    explicit WeatherView(PepelEQAudioProcessor& p) : proc(p)
    {
        juce::Random r(777);
        for (int i = 0; i < 7; ++i)
        {
            Cloud c; c.x = r.nextFloat() * 1150.0f; c.y = 4 + r.nextInt(8) * 4 + (i % 2) * 6;
            c.size = i % 3; c.speed = 0.3f + r.nextFloat() * 0.8f;
            clouds.push_back(c);
        }
        for (int i = 0; i < 70; ++i) rain.push_back({ r.nextFloat() * 1150.0f, r.nextFloat() * 64.0f, 6.0f + r.nextFloat() * 5.0f });
    }

    void tick()
    {
        auto& rnd = juce::Random::getSystemRandom();
        const float target = proc.outAnalyzer.dirt.load();
        dirt += (target - dirt) * 0.1f;

        for (auto& c : clouds) { c.x += c.speed * (1.0f + dirt); if (c.x > 1170.0f) c.x = -100.0f; }
        for (auto& d : rain)
        {
            d.y += d.v; d.x -= 3.0f;
            if (d.y > 70.0f || d.x < 0.0f) { d.y = -rnd.nextFloat() * 20.0f; d.x = rnd.nextFloat() * 1180.0f; }
        }
        if (flash > 0) --flash;
        if (dirt >= 0.85f && flash == 0 && rnd.nextFloat() < 0.12f)
        {
            flash = 2;
            bolt.clear();
            float x = 80.0f + rnd.nextFloat() * 990.0f;
            for (float y = 0.0f; y < 66.0f; y += 8.0f)
            {
                bolt.push_back({ x, y });
                x += (rnd.nextBool() ? 1.0f : -1.0f) * (4.0f + rnd.nextFloat() * 8.0f);
            }
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        drawPanelFrame(g, b, col::frameViolet);
        auto inner = b.reduced(7);
        g.saveState();
        g.reduceClipRegion(inner);
        g.setOrigin(inner.getPosition());
        paintScene(g, inner.getWidth(), inner.getHeight());
        g.restoreState();
    }

private:
    static constexpr int nCols = 189;
    static constexpr int U = 4;
    struct Cloud { float x; int y; int size; float speed; };
    struct Drop { float x, y, v; };

    void paintScene(juce::Graphics& g, int w, int h)
    {
        const float d = juce::jlimit(0.0f, 1.0f, dirt);
        const int horizon = 64;

        static const float kp[6] = { 0.0f, 0.2f, 0.4f, 0.6f, 0.85f, 1.0f };
        static const juce::Colour topC[6] = { hex(0x9ED8F5), hex(0x3A56C8), hex(0x6A3FA8), hex(0x3A0A30), hex(0x2A0010), hex(0x2A0010) };
        static const juce::Colour horC[6] = { hex(0xE6F6FF), hex(0xF2803E), hex(0xF08A4A), hex(0x8A1030), hex(0xC10F1D), hex(0xC10F1D) };
        static const juce::Colour cloudC[6] = { hex(0xF4FAFF), hex(0xE6D7F5), hex(0x9C7FDA), hex(0x6A2050), hex(0x3E0E26), hex(0x3E0E26) };

        const juce::Colour top = keyframes(kp, topC, d), hor = keyframes(kp, horC, d), cc = keyframes(kp, cloudC, d);
        for (int y = 0; y < h; y += U)
        {
            g.setColour(lerpC(top, hor, juce::jmin(1.0f, (float) y / (float) horizon)));
            g.fillRect(0, y, w, U);
        }

        if (d < 0.65f)
        {
            const float t = juce::jlimit(0.0f, 1.0f, d / 0.5f);
            const int cy = (int) (24 + t * 66);
            const juce::Colour sc = lerpC(lerpC(hex(0xFFF6B8), hex(0xFFB347), t * 2.0f), col::coral, t > 0.5f ? (t - 0.5f) * 2.0f : 0.0f);
            pixelCircle(g, 150, cy, 8, sc.withAlpha(0.22f));
            pixelCircle(g, 150, cy, 5, sc);
        }
        if (d > 0.8f)
        {
            const float a = juce::jlimit(0.0f, 1.0f, (d - 0.8f) / 0.15f);
            pixelCircle(g, w * 4 / 5, 26, 8, col::blood.withAlpha(0.25f * a));
            pixelCircle(g, w * 4 / 5, 26, 6, col::crimson.withAlpha(a));
            g.setColour(col::maroon.withAlpha(a));
            g.fillRect(w * 4 / 5 - 8, 18, 8, 4); g.fillRect(w * 4 / 5 + 4, 28, 8, 8); g.fillRect(w * 4 / 5 - 12, 32, 4, 4);
        }
        const int visible = 3 + juce::roundToInt(d * 4.0f);
        for (int i = 0; i < (int) clouds.size() && i < visible; ++i)
        {
            auto& c = clouds[(size_t) i];
            const char* const* rows = c.size == 0 ? sprites::cloudS : (c.size == 1 ? sprites::cloudM : sprites::cloudL);
            const int cw = c.size == 0 ? sprites::cloudSW : (c.size == 1 ? sprites::cloudMW : sprites::cloudLW);
            const int ch = c.size == 0 ? sprites::cloudSH : (c.size == 1 ? sprites::cloudMH : sprites::cloudLH);
            drawSprite(g, rows, cw, ch, (int) c.x, c.y, U, cc);
        }
        if (d > 0.55f)
        {
            const int n = juce::roundToInt((d - 0.55f) / 0.45f * (float) rain.size());
            g.setColour(col::glow.withAlpha(0.55f));
            for (int i = 0; i < n && i < (int) rain.size(); ++i)
                g.fillRect((int) rain[(size_t) i].x, (int) rain[(size_t) i].y, 2, 6);
        }
        if (flash > 0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.fillRect(0, 0, w, h);
            for (size_t i = 0; i + 1 < bolt.size(); ++i)
            {
                g.setColour(col::glow.withAlpha(0.6f));
                g.fillRect((int) bolt[i].x - 4, (int) bolt[i].y, 12, 10);
                g.setColour(col::moon);
                g.fillRect((int) bolt[i].x, (int) bolt[i].y, 4, 10);
                g.fillRect((int) juce::jmin(bolt[i].x, bolt[i + 1].x), (int) bolt[i].y + 8, (int) std::abs(bolt[i + 1].x - bolt[i].x) + 4, 4);
            }
        }

        // осциллограмма выхода (динамика звуковой волны) вместо чёрных столбиков
        const int head = proc.scopeHead.load(std::memory_order_acquire);
        const int centre = h - 30;
        const juce::Colour tip = lerpC(col::glow, col::coral, d);
        for (int i = 0; i < nCols; ++i)
        {
            const float pk = proc.scope[(size_t) ((head + i) % PepelEQAudioProcessor::kScope)].load();
            const float db = 20.0f * std::log10(juce::jmax(pk, 1.0e-4f));
            const float l = juce::jlimit(0.0f, 1.0f, (db + 54.0f) / 54.0f);
            const int half = 2 + ((int) (l * 26.0f) / 2) * 2;
            g.setColour(col::ink);
            g.fillRect(i * 6, centre - half, 5, half * 2);
            g.setColour(tip.withAlpha(0.85f));
            g.fillRect(i * 6, centre - half, 5, 2);
            g.fillRect(i * 6, centre + half - 2, 5, 2);
        }
    }

    static void pixelCircle(juce::Graphics& g, int cx, int cy, int r, juce::Colour c)
    {
        g.setColour(c);
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x * x + y * y <= r * r)
                    g.fillRect(cx + x * U, cy + y * U, U, U);
    }

    PepelEQAudioProcessor& proc;
    float dirt = 0.0f;
    int flash = 0;
    std::vector<Cloud> clouds;
    std::vector<Drop> rain;
    std::vector<juce::Point<float>> bolt;
};
} // namespace pepel
