#pragma once
#include <JuceHeader.h>
#include "Analyzer.h"

constexpr int kPts = 8;   // точек, которые пользователь назначает сам

namespace ids
{
    inline juce::String pOn(int i)    { return "pOn" + juce::String(i); }
    inline juce::String pF(int i)     { return "pF" + juce::String(i); }
    inline juce::String pG(int i)     { return "pG" + juce::String(i); }
    inline juce::String pQ(int i)     { return "pQ" + juce::String(i); }
    inline juce::String pNotch(int i) { return "pNt" + juce::String(i); }
    inline juce::String pDyn(int i)   { return "pDy" + juce::String(i); }
    inline juce::String pThr(int i)   { return "pTh" + juce::String(i); }
    inline const char* hpOn = "hpOn";
    inline const char* hpF = "hpF";
    inline const char* lpOn = "lpOn";
    inline const char* lpF = "lpF";
    inline const char* slope = "slope";
    inline const char* dynAtk = "dynA";
    inline const char* dynRel = "dynR";
    inline const char* scOn = "scOn";
    inline const char* outGain = "out";
}

// код прослушивания: 0..kPts-1 точки, 100 = что убирает срез низа, 101 = что убирает срез верха, -1 = выкл
constexpr int kListenHP = 100, kListenLP = 101;

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

    juce::AudioProcessorValueTreeState apvts;
    Analyzer inAnalyzer, outAnalyzer;

    // осциллограмма выхода (пик за 25 мс), для нижней плашки
    static constexpr int kScope = 190;
    std::array<std::atomic<float>, kScope> scope;
    std::atomic<int> scopeHead { 0 };

    std::atomic<float> dynGainOut[kPts] = {};   // текущее усиление динамических точек
    std::atomic<int> listen { -1 };             // что слушаем соло
    std::atomic<float> scLevel { 0.0f };        // уровень сигнала на входе сайдчейна (линейный пик)
    std::atomic<bool> scBusOn { false };        // вход сайдчейна включён хостом

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
        void setLowpass(double sr, double f, double q);
        void setBandpass(double sr, double f, double q);
    };

    double sampleRate = 44100.0;
    int currentProgram = 0;

    static constexpr int kMaxStages = 4;
    Biquad pt[kPts], det[kPts], hp[kMaxStages], lp[kMaxStages], lb[2];
    float ptLastG[kPts], ptLastF[kPts], ptLastQ[kPts];
    float dynGain[kPts], dynEnv[kPts];
    float lastHpF = -1, lastLpF = -1, lastSlope = -1, lastLbF = -1, lastLbQ = -1;
    float outGainSmoothed = 1.0f;

    std::atomic<float>* pOnP[kPts]; std::atomic<float>* pFP[kPts]; std::atomic<float>* pGP[kPts];
    std::atomic<float>* pQP[kPts]; std::atomic<float>* pNP[kPts]; std::atomic<float>* pDP[kPts]; std::atomic<float>* pTP[kPts];
    std::atomic<float> *pHpOn, *pHpF, *pLpOn, *pLpF, *pSlope, *pAtk, *pRel, *pSc, *pOut;

    float scopeAcc = 0.0f;
    int scopeCount = 0, scopeSlice = 1100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PepelEQAudioProcessor)
};
