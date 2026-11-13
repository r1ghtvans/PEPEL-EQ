#pragma once
#include <JuceHeader.h>
#include "Analyzer.h"

namespace ids
{
    inline juce::String gain(int i)    { return "g" + juce::String(i); }
    inline juce::String dFreq(int i)   { return "dF" + juce::String(i); }
    inline juce::String dThr(int i)    { return "dT" + juce::String(i); }
    inline juce::String dRng(int i)    { return "dR" + juce::String(i); }
    inline juce::String nOn(int i)     { return "nO" + juce::String(i); }
    inline juce::String nFreq(int i)   { return "nF" + juce::String(i); }
    inline const char* subOn = "subOn";
    inline const char* subFreq = "subF";
    inline const char* dynQ = "dynQ";
    inline const char* dynAtk = "dynA";
    inline const char* dynRel = "dynR";
    inline const char* scOn = "scOn";
    inline const char* nDepth = "nDepth";
    inline const char* outGain = "out";
}

constexpr int kGraphic = 10;
constexpr int kDyn = 4;
constexpr int kNotch = 4;
inline constexpr float kGraphicFreqs[kGraphic] = { 31.5f, 63.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f };

class PepelEQAudioProcessor : public juce::AudioProcessor
{
public:
    PepelEQAudioProcessor();
    ~PepelEQAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Pepel EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // добавить режекторный фильтр на частоту (кнопка "убрать" из поиска шума)
    void addNotchAt(float freqHz);

    juce::AudioProcessorValueTreeState apvts;
    Analyzer inAnalyzer, outAnalyzer;
    // осциллограмма выхода (пик за 25 мс), для нижней плашки
    static constexpr int kScope = 190;
    std::array<std::atomic<float>, kScope> scope;
    std::atomic<int> scopeHead { 0 };
    std::atomic<float> dynGainOut[kDyn] = {};   // текущее усиление динамических точек (для кривой)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] = { 0, 0 }, z2[2] = { 0, 0 };
        inline float process(int ch, float xf)
        {
            const double x = xf;
            const double y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
        void set(double nb0, double nb1, double nb2, double a0, double na1, double na2)
        {
            b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
        }
        void setPeaking(double sr, double f, double q, double gainDb);
        void setHighpass(double sr, double f, double q);
        void setBandpass(double sr, double f, double q);
    };

    double sampleRate = 44100.0;
    int currentProgram = 0;

    Biquad sub[2], graphic[kGraphic], dyn[kDyn], det[kDyn], notch[kNotch];
    float lastGraphic[kGraphic], dynGain[kDyn], dynLastApplied[kDyn], dynEnv[kDyn];
    float lastSubFreq = -1.0f, lastNotchF[kNotch], lastNotchOn[kNotch], lastNotchDepth = -1.0f;
    float lastDynF[kDyn], lastDynQ = -1.0f;
    float outGainSmoothed = 1.0f;
    int notchRotate = 0;

    std::atomic<float>* pGain[kGraphic];
    std::atomic<float>* pDF[kDyn]; std::atomic<float>* pDT[kDyn]; std::atomic<float>* pDR[kDyn];
    std::atomic<float>* pNO[kNotch]; std::atomic<float>* pNF[kNotch];
    std::atomic<float> *pSubOn, *pSubF, *pDynQ, *pDynA, *pDynR, *pScOn, *pNDepth, *pOut;

    juce::AudioBuffer<float> scratch;
    float scopeAcc = 0.0f;
    int scopeCount = 0, scopeSlice = 1100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PepelEQAudioProcessor)
};
