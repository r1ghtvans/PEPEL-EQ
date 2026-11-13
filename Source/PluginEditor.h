#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Rows.h"
#include "Views.h"
#include "EqDisplay.h"

class PepelEQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PepelEQEditor(PepelEQAudioProcessor&);
    ~PepelEQEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    pepel::Panel* addPanel(const juce::String& title, juce::Colour frame, int x, int y, int nRows);
    pepel::Row* addSlider(pepel::Panel*, const juce::String& name, const juce::String& id,
                          std::function<juce::String(float)> fmt, const juce::String& toggleId = {});
    pepel::Row* addButton(pepel::Panel*, const juce::String& name, std::function<bool()> on, std::function<juce::String()> text, std::function<void()> click);
    pepel::Row* addToggle(pepel::Panel*, const juce::String& name, const juce::String& id,
                          const juce::String& onText, const juce::String& offText);

    PepelEQAudioProcessor& proc;
    pepel::HeaderView header;
    pepel::WeatherView weather;
    pepel::EqDisplay display;
    pepel::Row* pointRows[4] = { nullptr, nullptr, nullptr, nullptr };
    void applySelection(pepel::EqDisplay::Sel);
    std::vector<std::unique_ptr<pepel::Panel>> panels;

    juce::Image background, scanlines;
    int glitchFrames = 0, nextGlitch = 60;
    juce::Rectangle<int> glitchBar;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PepelEQEditor)
};
