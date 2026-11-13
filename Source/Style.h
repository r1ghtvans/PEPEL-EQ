#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"
#include "Sprites.h"

// Русский текст в исходниках: UTF-8 + /utf-8 для MSVC
#define RU(s) juce::String::fromUTF8(s)

namespace pepel
{
// ---------- палитра ----------
inline juce::Colour hex(juce::uint32 rgb) { return juce::Colour(0xFF000000u | rgb); }

namespace col
{
    inline const juce::Colour ink      = hex(0x040533);
    inline const juce::Colour night    = hex(0x08095B);
    inline const juce::Colour indigo   = hex(0x232284);
    inline const juce::Colour violetDk = hex(0x362A8E);
    inline const juce::Colour mist     = hex(0x60579E);
    inline const juce::Colour glow     = hex(0xB7A6F0);
    inline const juce::Colour moon     = hex(0xEDE4FF);
    inline const juce::Colour starBlue = hex(0x4F8FD0);
    inline const juce::Colour crimson  = hex(0xC10F1D);
    inline const juce::Colour blood    = hex(0xE93426);
    inline const juce::Colour coral    = hex(0xE8634A);
    inline const juce::Colour peach    = hex(0xEEA380);
    inline const juce::Colour maroon   = hex(0x730426);
    inline const juce::Colour deepRed  = hex(0x530121);
    inline const juce::Colour paper    = hex(0x0B0C46);
    inline const juce::Colour track    = hex(0x1D1F78);
    inline const juce::Colour text     = hex(0xE9E4FF);
    inline const juce::Colour textSoft = hex(0xEEA380);
    inline const juce::Colour frameBlue   = hex(0x4F72E6);
    inline const juce::Colour frameBlood  = hex(0xE93426);
    inline const juce::Colour frameViolet = hex(0x7F6FE0);
    inline const juce::Colour frameCoral  = hex(0xE8634A);
    inline const juce::Colour cyan = hex(0x7FE7FF);
    inline const juce::Colour pink = hex(0xFF7FBF);
    inline const juce::Colour red  = hex(0xFF4A5A);
}

// ---------- шрифты ----------
struct Fonts
{
    juce::Typeface::Ptr vt, ps;
    Fonts()
    {
        vt = juce::Typeface::createSystemTypefaceFor(BinaryData::VT323Regular_ttf, BinaryData::VT323Regular_ttfSize);
        ps = juce::Typeface::createSystemTypefaceFor(BinaryData::PressStart2PRegular_ttf, BinaryData::PressStart2PRegular_ttfSize);
    }
    static Fonts& get() { static Fonts f; return f; }
    juce::Font vt323(float h) { return juce::Font(juce::FontOptions(vt).withHeight(h)); }
    juce::Font press(float h) { return juce::Font(juce::FontOptions(ps).withHeight(h)); }
};

inline bool isAscii(const juce::String& s)
{
    for (auto c : s) if (c > 127) return false;
    return true;
}

// Латиница/цифры -> VT323, кириллица -> Press Start 2P
inline void drawMixed(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r,
                      juce::Justification j, float vtSize = 25.0f, float psSize = 10.0f)
{
    g.setFont(isAscii(t) ? Fonts::get().vt323(vtSize) : Fonts::get().press(psSize));
    g.drawText(t, r, j, false);
}

// ---------- пиксельная графика ----------
inline void drawSprite(juce::Graphics& g, const char* const* rows, int w, int h, int x, int y, int scale, juce::Colour c)
{
    g.setColour(c);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            if (rows[j][i] == '#')
                g.fillRect(x + i * scale, y + j * scale, scale, scale);
}

// Прямоугольник со «ступенчатыми» углами (шаг cut/2)
inline void fillStepped(juce::Graphics& g, juce::Rectangle<int> r, int cut)
{
    if (cut <= 0) { g.fillRect(r); return; }
    const int h2 = cut / 2;
    g.fillRect(r.getX() + cut, r.getY(), r.getWidth() - 2 * cut, r.getHeight());
    g.fillRect(r.getX() + h2, r.getY() + h2, r.getWidth() - 2 * h2, r.getHeight() - 2 * h2);
    g.fillRect(r.getX(), r.getY() + cut, r.getWidth(), r.getHeight() - 2 * cut);
}

inline int cutFor(int inset) { return juce::jmax(0, 6 - (inset / 2) * 2); }

// Двойная рамка 3+2+2, заливка paper
inline void drawPanelFrame(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour frame)
{
    g.setColour(frame);               fillStepped(g, r.reduced(0), cutFor(0));
    g.setColour(col::paper);          fillStepped(g, r.reduced(3), cutFor(3));
    g.setColour(frame);               fillStepped(g, r.reduced(5), cutFor(5));
    g.setColour(col::paper);          fillStepped(g, r.reduced(7), cutFor(7));
}

inline void drawPanelShadow(juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour(col::ink.withAlpha(0.55f));
    fillStepped(g, r.translated(4, 4), 6);
}

inline juce::Colour lerpC(juce::Colour a, juce::Colour b, float t)
{
    return a.interpolatedWith(b, juce::jlimit(0.0f, 1.0f, t));
}

// Многоточечный градиент по ключевым кадрам
template <size_t N>
inline juce::Colour keyframes(const float (&pos)[N], const juce::Colour (&cols)[N], float t)
{
    if (t <= pos[0]) return cols[0];
    for (size_t i = 1; i < N; ++i)
        if (t <= pos[i])
            return lerpC(cols[i - 1], cols[i], (t - pos[i - 1]) / (pos[i] - pos[i - 1]));
    return cols[N - 1];
}

// Фон: горизонтальные полосы по 6 px, 14 ступеней, без сглаживания
inline juce::Image makeBackground(int w, int h)
{
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Graphics g(img);
    const float pos[4] = { 0.0f, 0.38f, 0.72f, 1.0f };
    const juce::Colour cs[4] = { col::ink, hex(0x1E2181), col::maroon, col::crimson };
    for (int y = 0; y < h; y += 6)
    {
        float t = (float) y / (float) h;
        t = std::floor(t * 14.0f) / 13.0f;
        g.setColour(keyframes(pos, cs, juce::jlimit(0.0f, 1.0f, t)));
        g.fillRect(0, y, w, 6);
    }
    return img;
}
} // namespace pepel
