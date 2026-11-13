#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Style.h"

// ======================= пресеты =======================
namespace
{
struct DynSet { float f, thr, rng; };
struct Preset
{
    const char* name;
    float g[kGraphic];
    bool sub;
    DynSet dyn[kDyn];   // rng == 0: не трогать (выключено)
};

const Preset kPresets[] = {
    { "ЧИСТЫЙ",   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },            false, { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    { "ГОЛОС",    { 0, 0, -2, -2, 0, 1, 2, 3, 2, 1 },          true,  { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    { "ТЁПЛЫЙ",   { 2, 2, 2, 1, 0, 0, -1, -2, -2, -3 },        false, { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    { "ЯРКИЙ",    { 0, 0, -1, -1, 0, 1, 2, 4, 5, 5 },          false, { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    { "БАСС",     { 5, 5, 3, 1, 0, 0, 0, 0, 0, 0 },            true,  { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    { "ТЕЛЕФОН",  { -12, -12, -10, -4, 2, 4, 3, -3, -12, -12 }, false, { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    { "ДЕ-ЭССЕР", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },            true,  { {0,0,0}, {0,0,0}, {0,0,0}, {7000.f, -35.f, -8.f} } },
    { "ЧИСТКА",   { 0, 0, 0, -1, -1, 0, 0, 0, 0, 0 },         true,  { {300.f, -30.f, -5.f}, {0,0,0}, {3500.f, -32.f, -5.f}, {0,0,0} } },
};
constexpr int kNumPresets = (int) (sizeof(kPresets) / sizeof(kPresets[0]));

void setReal(juce::AudioProcessorValueTreeState& s, const juce::String& id, float v)
{
    if (auto* p = s.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}
} // namespace

// ======================= биквады (RBJ) =======================
void PepelEQAudioProcessor::Biquad::setPeaking(double sr, double f, double q, double gainDb)
{
    f = juce::jlimit(10.0, sr * 0.45, f);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
    const double al = std::sin(w0) / (2.0 * q), c = std::cos(w0);
    set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}
void PepelEQAudioProcessor::Biquad::setHighpass(double sr, double f, double q)
{
    f = juce::jlimit(10.0, sr * 0.45, f);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
    const double al = std::sin(w0) / (2.0 * q), c = std::cos(w0);
    set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}
void PepelEQAudioProcessor::Biquad::setBandpass(double sr, double f, double q)
{
    f = juce::jlimit(10.0, sr * 0.45, f);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
    const double al = std::sin(w0) / (2.0 * q), c = std::cos(w0);
    set(al, 0, -al, 1 + al, -2 * c, 1 - al);
}

// ======================= параметры =======================
juce::AudioProcessorValueTreeState::ParameterLayout PepelEQAudioProcessor::createLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;

    auto freqRange = [](float lo, float hi, float centre) {
        NormalisableRange<float> r(lo, hi, 0.0f, 1.0f);
        r.setSkewForCentre(centre);
        return r;
    };

    v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ ids::subOn, 1 }, "Sub cut", true));
    v.push_back(std::make_unique<P>(ParameterID{ ids::subFreq, 1 }, "Sub cut freq", NormalisableRange<float>(20.f, 60.f, 0.5f), 30.f));
    v.push_back(std::make_unique<P>(ParameterID{ ids::outGain, 1 }, "Output", NormalisableRange<float>(-12.f, 12.f, 0.1f), 0.f));

    for (int i = 0; i < kGraphic; ++i)
        v.push_back(std::make_unique<P>(ParameterID{ ids::gain(i), 1 }, "Band " + String(kGraphicFreqs[i]) + " Hz",
                                        NormalisableRange<float>(-12.f, 12.f, 0.1f), 0.f));

    const float dDef[kDyn] = { 200.f, 1000.f, 4000.f, 8000.f };
    for (int i = 0; i < kDyn; ++i)
    {
        const String n = "Dyn " + String(i + 1);
        v.push_back(std::make_unique<P>(ParameterID{ ids::dFreq(i), 1 }, n + " freq", freqRange(40.f, 16000.f, 1500.f), dDef[i]));
        v.push_back(std::make_unique<P>(ParameterID{ ids::dThr(i), 1 },  n + " threshold", NormalisableRange<float>(-70.f, 0.f, 0.5f), -30.f));
        v.push_back(std::make_unique<P>(ParameterID{ ids::dRng(i), 1 },  n + " effect", NormalisableRange<float>(-18.f, 18.f, 0.5f), 0.f));
    }
    v.push_back(std::make_unique<P>(ParameterID{ ids::dynQ, 1 },   "Dyn Q", freqRange(0.3f, 10.f, 2.f), 2.f));
    v.push_back(std::make_unique<P>(ParameterID{ ids::dynAtk, 1 }, "Dyn attack", freqRange(1.f, 100.f, 15.f), 10.f));
    v.push_back(std::make_unique<P>(ParameterID{ ids::dynRel, 1 }, "Dyn release", freqRange(20.f, 800.f, 150.f), 150.f));
    v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ ids::scOn, 1 }, "Sidechain input", false));

    for (int i = 0; i < kNotch; ++i)
    {
        v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ ids::nOn(i), 1 }, "Notch " + String(i + 1) + " on", false));
        v.push_back(std::make_unique<P>(ParameterID{ ids::nFreq(i), 1 }, "Notch " + String(i + 1) + " freq", freqRange(20.f, 20000.f, 1000.f), 1000.f));
    }
    v.push_back(std::make_unique<P>(ParameterID{ ids::nDepth, 1 }, "Notch depth", NormalisableRange<float>(6.f, 36.f, 0.5f), 18.f));

    return { v.begin(), v.end() };
}

// ======================= процессор =======================
PepelEQAudioProcessor::PepelEQAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "PEPEL", createLayout())
{
    for (int i = 0; i < kGraphic; ++i) pGain[i] = apvts.getRawParameterValue(ids::gain(i));
    for (int i = 0; i < kDyn; ++i)
    {
        pDF[i] = apvts.getRawParameterValue(ids::dFreq(i));
        pDT[i] = apvts.getRawParameterValue(ids::dThr(i));
        pDR[i] = apvts.getRawParameterValue(ids::dRng(i));
    }
    for (int i = 0; i < kNotch; ++i)
    {
        pNO[i] = apvts.getRawParameterValue(ids::nOn(i));
        pNF[i] = apvts.getRawParameterValue(ids::nFreq(i));
    }
    pSubOn = apvts.getRawParameterValue(ids::subOn);
    pSubF = apvts.getRawParameterValue(ids::subFreq);
    pDynQ = apvts.getRawParameterValue(ids::dynQ);
    pDynA = apvts.getRawParameterValue(ids::dynAtk);
    pDynR = apvts.getRawParameterValue(ids::dynRel);
    pScOn = apvts.getRawParameterValue(ids::scOn);
    pNDepth = apvts.getRawParameterValue(ids::nDepth);
    pOut = apvts.getRawParameterValue(ids::outGain);

    for (auto& s : scope) s.store(0.0f);
    outAnalyzer.computeDirt = true;
    inAnalyzer.doSearch = true;
    for (int i = 0; i < kGraphic; ++i) lastGraphic[i] = 1000.f;
    for (int i = 0; i < kDyn; ++i) { dynGain[i] = 0; dynLastApplied[i] = 1000.f; dynEnv[i] = 0; lastDynF[i] = -1.f; }
    for (int i = 0; i < kNotch; ++i) { lastNotchF[i] = -1.f; lastNotchOn[i] = -1.f; }
}

bool PepelEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.getChannelSet(true, 1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

void PepelEQAudioProcessor::prepareToPlay(double sr, int)
{
    sampleRate = sr;
    scopeSlice = juce::jmax(64, (int) (sr * 0.025));
    scopeAcc = 0.0f; scopeCount = 0;
    inAnalyzer.prepare(sr);
    outAnalyzer.prepare(sr);
    for (auto& b : sub) b.reset();
    for (auto& b : graphic) b.reset();
    for (auto& b : dyn) b.reset();
    for (auto& b : det) b.reset();
    for (auto& b : notch) b.reset();
    for (int i = 0; i < kGraphic; ++i) lastGraphic[i] = 1000.f;
    for (int i = 0; i < kDyn; ++i) { dynGain[i] = 0; dynLastApplied[i] = 1000.f; dynEnv[i] = 0; lastDynF[i] = -1.f; }
    for (int i = 0; i < kNotch; ++i) { lastNotchF[i] = -1.f; lastNotchOn[i] = -1.f; }
    lastSubFreq = -1.f; lastNotchDepth = -1.f; lastDynQ = -1.f;
    outGainSmoothed = juce::Decibels::decibelsToGain(pOut->load());
}

void PepelEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto mainBuf = getBusBuffer(buffer, true, 0);
    const int nch = juce::jmin(2, mainBuf.getNumChannels());
    if (nch <= 0 || n <= 0) return;

    // боковая цепь
    const float* scPtr[2] = { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount(true) > 1)
        if (auto* b = getBus(true, 1))
            if (b->isEnabled())
            {
                auto sc = getBusBuffer(buffer, true, 1);
                scCh = juce::jmin(2, sc.getNumChannels());
                for (int c = 0; c < scCh; ++c) scPtr[c] = sc.getReadPointer(c);
            }
    const bool useSc = pScOn->load() > 0.5f && scCh > 0;

    float* ch[2] = { mainBuf.getWritePointer(0), nch > 1 ? mainBuf.getWritePointer(1) : nullptr };
    const float* chIn[2] = { ch[0], ch[1] };
    inAnalyzer.push(chIn, nch, n);

    // ---- коэффициенты ----
    const double sr = sampleRate;
    const bool subOn = pSubOn->load() > 0.5f;
    const float subF = pSubF->load();
    if (subF != lastSubFreq)
    {
        sub[0].setHighpass(sr, subF, 0.5412);
        sub[1].setHighpass(sr, subF, 1.3066);
        lastSubFreq = subF;
    }
    bool gActive[kGraphic];
    for (int i = 0; i < kGraphic; ++i)
    {
        const float g = pGain[i]->load();
        if (g != lastGraphic[i])
        {
            graphic[i].setPeaking(sr, kGraphicFreqs[i], 1.4, g);
            if (std::abs(g) < 0.01f) graphic[i].reset();
            lastGraphic[i] = g;
        }
        gActive[i] = std::abs(g) >= 0.01f;
    }
    const float depth = pNDepth->load();
    bool nActive[kNotch];
    for (int i = 0; i < kNotch; ++i)
    {
        const float on = pNO[i]->load() > 0.5f ? 1.f : 0.f, f = pNF[i]->load();
        if (on != lastNotchOn[i] || f != lastNotchF[i] || depth != lastNotchDepth)
        {
            notch[i].setPeaking(sr, f, 12.0, -depth);
            if (on < 0.5f) notch[i].reset();
            lastNotchOn[i] = on; lastNotchF[i] = f;
        }
        nActive[i] = on > 0.5f;
    }
    lastNotchDepth = depth;

    const float dq = pDynQ->load();
    bool dActive[kDyn];
    float dThr[kDyn], dRng[kDyn];
    for (int i = 0; i < kDyn; ++i)
    {
        const float f = pDF[i]->load();
        dThr[i] = pDT[i]->load();
        dRng[i] = pDR[i]->load();
        dActive[i] = std::abs(dRng[i]) >= 0.05f;
        if (f != lastDynF[i] || dq != lastDynQ)
        {
            det[i].setBandpass(sr, f, dq);
            dynLastApplied[i] = 1000.f;      // принудительно пересчитать
            lastDynF[i] = f;
        }
        if (!dActive[i]) { dynGain[i] = 0.f; dynEnv[i] = 0.f; dyn[i].reset(); det[i].reset(); dynLastApplied[i] = 1000.f; }
    }
    lastDynQ = dq;

    const float atk = std::exp(-1.0f / ((float) sr * juce::jmax(0.1f, pDynA->load()) * 0.001f));
    const float rel = std::exp(-1.0f / ((float) sr * juce::jmax(1.0f, pDynR->load()) * 0.001f));

    const float outTarget = juce::Decibels::decibelsToGain(pOut->load());
    const float outStep = (outTarget - outGainSmoothed) / (float) n;
    float outG = outGainSmoothed;

    // ---- обработка кусками по 32 сэмпла ----
    constexpr int chunk = 32;
    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin(chunk, n - start);

        // динамические полосы: пересчёт усиления по огибающей
        for (int d = 0; d < kDyn; ++d)
        {
            if (!dActive[d]) continue;
            const float lv = 20.0f * std::log10(juce::jmax(dynEnv[d], 1.0e-6f));
            const float over = lv - dThr[d];
            const float sgn = dRng[d] > 0 ? 1.0f : -1.0f;
            const float target = over > 0.0f ? sgn * juce::jmin(std::abs(dRng[d]), over) : 0.0f;
            dynGain[d] += (target - dynGain[d]) * 0.35f;
            if (std::abs(dynGain[d] - dynLastApplied[d]) > 0.02f || dynLastApplied[d] > 999.f)
            {
                dyn[d].setPeaking(sr, lastDynF[d], dq, dynGain[d]);
                dynLastApplied[d] = dynGain[d];
            }
        }

        for (int i = start; i < start + len; ++i)
        {
            // детекторы читают ИСХОДНЫЙ сигнал (или сайдчейн)
            for (int d = 0; d < kDyn; ++d)
            {
                if (!dActive[d]) continue;
                float lvl = 0.0f;
                for (int c = 0; c < nch; ++c)
                {
                    const float src = useSc ? scPtr[juce::jmin(c, scCh - 1)][i] : ch[c][i];
                    lvl = juce::jmax(lvl, std::abs(det[d].process(c, src)));
                }
                float& e = dynEnv[d];
                e = lvl > e ? atk * e + (1.0f - atk) * lvl : rel * e + (1.0f - rel) * lvl;
            }

            for (int c = 0; c < nch; ++c)
            {
                float x = ch[c][i];
                if (subOn) { x = sub[0].process(c, x); x = sub[1].process(c, x); }
                for (int b = 0; b < kGraphic; ++b) if (gActive[b]) x = graphic[b].process(c, x);
                for (int d = 0; d < kDyn; ++d)     if (dActive[d]) x = dyn[d].process(c, x);
                for (int k = 0; k < kNotch; ++k)   if (nActive[k]) x = notch[k].process(c, x);
                ch[c][i] = x * outG;
            }
            outG += outStep;
        }
    }
    outGainSmoothed = outTarget;
    for (int i = 0; i < n; ++i)
    {
        float v = std::abs(ch[0][i]);
        if (nch > 1) v = juce::jmax(v, std::abs(ch[1][i]));
        scopeAcc = juce::jmax(scopeAcc, v);
        if (++scopeCount >= scopeSlice)
        {
            const int hd = scopeHead.load(std::memory_order_relaxed);
            scope[(size_t) hd].store(scopeAcc);
            scopeHead.store((hd + 1) % kScope, std::memory_order_release);
            scopeAcc = 0.0f; scopeCount = 0;
        }
    }
    for (int d = 0; d < kDyn; ++d) dynGainOut[d].store(dynGain[d]);

    const float* chOut[2] = { ch[0], ch[1] };
    outAnalyzer.push(chOut, nch, n);
}

// ======================= поиск шума -> режектор =======================
void PepelEQAudioProcessor::addNotchAt(float f)
{
    // уже есть рядом?
    for (int i = 0; i < kNotch; ++i)
        if (pNO[i]->load() > 0.5f && std::abs(std::log2(pNF[i]->load() / f)) < 0.05f)
            return;
    int slot = -1;
    for (int i = 0; i < kNotch; ++i)
        if (pNO[i]->load() < 0.5f) { slot = i; break; }
    if (slot < 0) { slot = notchRotate; notchRotate = (notchRotate + 1) % kNotch; }

    auto* pf = apvts.getParameter(ids::nFreq(slot));
    auto* po = apvts.getParameter(ids::nOn(slot));
    pf->beginChangeGesture(); pf->setValueNotifyingHost(pf->convertTo0to1(f)); pf->endChangeGesture();
    po->beginChangeGesture(); po->setValueNotifyingHost(1.0f); po->endChangeGesture();
}

// ======================= пресеты =======================
int PepelEQAudioProcessor::getNumPrograms() { return kNumPresets; }
const juce::String PepelEQAudioProcessor::getProgramName(int i)
{
    return (i >= 0 && i < kNumPresets) ? RU(kPresets[i].name) : juce::String();
}
void PepelEQAudioProcessor::setCurrentProgram(int index)
{
    index = juce::jlimit(0, kNumPresets - 1, index);
    currentProgram = index;
    const auto& p = kPresets[index];
    for (int i = 0; i < kGraphic; ++i) setReal(apvts, ids::gain(i), p.g[i]);
    setReal(apvts, ids::subOn, p.sub ? 1.f : 0.f);
    const float dDef[kDyn] = { 200.f, 1000.f, 4000.f, 8000.f };
    for (int i = 0; i < kDyn; ++i)
    {
        if (p.dyn[i].rng != 0.f)
        {
            setReal(apvts, ids::dFreq(i), p.dyn[i].f);
            setReal(apvts, ids::dThr(i), p.dyn[i].thr);
            setReal(apvts, ids::dRng(i), p.dyn[i].rng);
        }
        else
        {
            setReal(apvts, ids::dFreq(i), dDef[i]);
            setReal(apvts, ids::dThr(i), -30.f);
            setReal(apvts, ids::dRng(i), 0.f);
        }
    }
}

// ======================= состояние =======================
void PepelEQAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}
void PepelEQAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml(*xml);
            currentProgram = juce::jlimit(0, kNumPresets - 1, (int) vt.getProperty("program", 0));
            apvts.replaceState(vt);
        }
}

juce::AudioProcessorEditor* PepelEQAudioProcessor::createEditor() { return new PepelEQEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PepelEQAudioProcessor(); }
