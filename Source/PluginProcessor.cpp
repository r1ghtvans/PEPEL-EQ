#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Style.h"

// ======================= пресеты =======================
namespace
{
struct PtPreset { float f, g, q; bool notch, dyn; float thr; };
struct Preset
{
    const char* name;
    float hp, lp;            // 0 = выключен
    PtPreset p[4];           // f == 0: точка не используется
};

const PtPreset kNone = { 0, 0, 0, false, false, 0 };
const Preset kPresets[] = {
    { "ЧИСТЫЙ",   0,   0,     { kNone, kNone, kNone, kNone } },
    { "ГОЛОС",    80,  0,     { { 200, -2.5f, 1.0f, false, false, 0 }, { 3000, 3.0f, 0.9f, false, false, 0 }, { 10000, 2.0f, 0.7f, false, false, 0 }, kNone } },
    { "ТЁПЛЫЙ",   0,   14000, { { 120, 2.5f, 0.8f, false, false, 0 }, { 4000, -2.0f, 0.8f, false, false, 0 }, kNone, kNone } },
    { "ЯРКИЙ",    0,   0,     { { 8000, 4.0f, 0.6f, false, false, 0 }, { 3000, 2.0f, 0.8f, false, false, 0 }, kNone, kNone } },
    { "БАСС",     30,  0,     { { 80, 5.0f, 0.9f, false, false, 0 }, { 300, -2.0f, 1.0f, false, false, 0 }, kNone, kNone } },
    { "ТЕЛЕФОН",  300, 3400,  { { 1500, 3.0f, 0.9f, false, false, 0 }, kNone, kNone, kNone } },
    { "ДЕ-ЭССЕР", 0,   0,     { { 7000, -8.0f, 2.0f, false, true, -35.0f }, kNone, kNone, kNone } },
    { "ЧИСТКА",   40,  0,     { { 300, -5.0f, 1.5f, false, true, -30.0f }, { 3500, -4.0f, 2.0f, false, true, -32.0f }, kNone, kNone } },
    { "ГУД 50 ГЦ", 0,  0,     { { 50, -24.0f, 12.0f, true, false, 0 }, { 100, -18.0f, 12.0f, true, false, 0 }, { 150, -14.0f, 12.0f, true, false, 0 }, kNone } },
};
constexpr int kNumPresets = (int) (sizeof(kPresets) / sizeof(kPresets[0]));

void setReal(juce::AudioProcessorValueTreeState& s, const juce::String& id, float v)
{
    if (auto* p = s.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

// Q для каскадов Баттерворта: 1 ступень (12 дБ), 2 (24 дБ), 4 (48 дБ)
const double kButterQ[3][4] = { { 0.7071, 0, 0, 0 }, { 0.5412, 1.3066, 0, 0 }, { 0.5098, 0.6013, 0.8999, 2.5629 } };
const int kStages[3] = { 1, 2, 4 };
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
void PepelEQAudioProcessor::Biquad::setLowpass(double sr, double f, double q)
{
    f = juce::jlimit(10.0, sr * 0.45, f);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
    const double al = std::sin(w0) / (2.0 * q), c = std::cos(w0);
    set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
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

    auto skewed = [](float lo, float hi, float centre) {
        NormalisableRange<float> r(lo, hi, 0.0f, 1.0f);
        r.setSkewForCentre(centre);
        return r;
    };

    v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ ids::hpOn, 1 }, "Low cut on", false));
    v.push_back(std::make_unique<P>(ParameterID{ ids::hpF, 1 }, "Low cut freq", skewed(20.f, 20000.f, 600.f), 30.f));
    v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ ids::lpOn, 1 }, "High cut on", false));
    v.push_back(std::make_unique<P>(ParameterID{ ids::lpF, 1 }, "High cut freq", skewed(20.f, 20000.f, 2500.f), 18000.f));
    v.push_back(std::make_unique<P>(ParameterID{ ids::slope, 1 }, "Cut slope", NormalisableRange<float>(0.f, 2.f, 1.f), 1.f));
    v.push_back(std::make_unique<P>(ParameterID{ ids::outGain, 1 }, "Output", NormalisableRange<float>(-12.f, 12.f, 0.1f), 0.f));

    for (int i = 0; i < kPts; ++i)
    {
        const String n = "Point " + String(i + 1);
        v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ ids::pOn(i), 1 }, n + " on", false));
        v.push_back(std::make_unique<P>(ParameterID{ ids::pF(i), 1 }, n + " freq", skewed(20.f, 20000.f, 1000.f), 1000.f));
        v.push_back(std::make_unique<P>(ParameterID{ ids::pG(i), 1 }, n + " gain", NormalisableRange<float>(-24.f, 24.f, 0.1f), 0.f));
        v.push_back(std::make_unique<P>(ParameterID{ ids::pQ(i), 1 }, n + " width (Q)", skewed(0.3f, 40.f, 3.f), 1.4f));
        v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ ids::pNotch(i), 1 }, n + " notch", false));
        v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ ids::pDyn(i), 1 }, n + " dynamic", false));
        v.push_back(std::make_unique<P>(ParameterID{ ids::pThr(i), 1 }, n + " threshold", NormalisableRange<float>(-70.f, 0.f, 0.5f), -30.f));
    }
    v.push_back(std::make_unique<P>(ParameterID{ ids::dynAtk, 1 }, "Dyn attack", skewed(1.f, 100.f, 15.f), 10.f));
    v.push_back(std::make_unique<P>(ParameterID{ ids::dynRel, 1 }, "Dyn release", skewed(20.f, 800.f, 150.f), 150.f));
    v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ ids::scOn, 1 }, "Sidechain input", false));

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
    for (int i = 0; i < kPts; ++i)
    {
        pOnP[i] = apvts.getRawParameterValue(ids::pOn(i));
        pFP[i] = apvts.getRawParameterValue(ids::pF(i));
        pGP[i] = apvts.getRawParameterValue(ids::pG(i));
        pQP[i] = apvts.getRawParameterValue(ids::pQ(i));
        pNP[i] = apvts.getRawParameterValue(ids::pNotch(i));
        pDP[i] = apvts.getRawParameterValue(ids::pDyn(i));
        pTP[i] = apvts.getRawParameterValue(ids::pThr(i));
    }
    pHpOn = apvts.getRawParameterValue(ids::hpOn);
    pHpF = apvts.getRawParameterValue(ids::hpF);
    pLpOn = apvts.getRawParameterValue(ids::lpOn);
    pLpF = apvts.getRawParameterValue(ids::lpF);
    pSlope = apvts.getRawParameterValue(ids::slope);
    pAtk = apvts.getRawParameterValue(ids::dynAtk);
    pRel = apvts.getRawParameterValue(ids::dynRel);
    pSc = apvts.getRawParameterValue(ids::scOn);
    pOut = apvts.getRawParameterValue(ids::outGain);

    for (auto& s : scope) s.store(0.0f);
    outAnalyzer.computeDirt = true;
    inAnalyzer.doSearch = false;
    for (int i = 0; i < kPts; ++i) { ptLastG[i] = 1000.f; ptLastF[i] = -1.f; ptLastQ[i] = -1.f; dynGain[i] = 0; dynEnv[i] = 0; }
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
    for (auto& b : pt) b.reset();
    for (auto& b : det) b.reset();
    for (auto& b : hp) b.reset();
    for (auto& b : lp) b.reset();
    for (auto& b : lb) b.reset();
    for (int i = 0; i < kPts; ++i) { ptLastG[i] = 1000.f; ptLastF[i] = -1.f; ptLastQ[i] = -1.f; dynGain[i] = 0; dynEnv[i] = 0; }
    lastHpF = lastLpF = lastSlope = lastLbF = lastLbQ = -1.f;
    outGainSmoothed = juce::Decibels::decibelsToGain(pOut->load());
}

void PepelEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto mainBuf = getBusBuffer(buffer, true, 0);
    const int nch = juce::jmin(2, mainBuf.getNumChannels());
    if (nch <= 0 || n <= 0) return;

    // ---- боковая цепь ----
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
    scBusOn.store(scCh > 0);
    {
        float pk = 0.0f;
        for (int c = 0; c < scCh; ++c) pk = juce::jmax(pk, scPtr[c] ? juce::FloatVectorOperations::findMaximum(scPtr[c], n) : 0.0f);
        scLevel.store(juce::jmax(pk, scLevel.load() * 0.85f));
    }
    const bool useSc = pSc->load() > 0.5f && scCh > 0;

    float* ch[2] = { mainBuf.getWritePointer(0), nch > 1 ? mainBuf.getWritePointer(1) : nullptr };
    const float* chIn[2] = { ch[0], ch[1] };
    inAnalyzer.push(chIn, nch, n);

    const double sr = sampleRate;

    // ---- срезы ----
    const int slopeIdx = juce::jlimit(0, 2, (int) std::lround(pSlope->load()));
    const int stages = kStages[slopeIdx];
    const bool hpOn = pHpOn->load() > 0.5f, lpOn = pLpOn->load() > 0.5f;
    const float hpF = pHpF->load(), lpF = pLpF->load();
    if (hpF != lastHpF || (float) slopeIdx != lastSlope)
        for (int s = 0; s < stages; ++s) hp[s].setHighpass(sr, hpF, kButterQ[slopeIdx][s]);
    if (lpF != lastLpF || (float) slopeIdx != lastSlope)
        for (int s = 0; s < stages; ++s) lp[s].setLowpass(sr, lpF, kButterQ[slopeIdx][s]);
    lastHpF = hpF; lastLpF = lpF; lastSlope = (float) slopeIdx;

    // ---- прослушивание ----
    int listenCode = listen.load();
    if (listenCode >= 0 && listenCode < kPts && pOnP[listenCode]->load() < 0.5f) listenCode = -1;
    if (listenCode == kListenHP && !hpOn) listenCode = -1;
    if (listenCode == kListenLP && !lpOn) listenCode = -1;
    if (listenCode != listen.load()) listen.store(listenCode);

    const float outTarget = juce::Decibels::decibelsToGain(pOut->load());
    const float outStep = (outTarget - outGainSmoothed) / (float) n;
    float outG = outGainSmoothed;

    if (listenCode >= 0)
    {
        if (listenCode < kPts)
        {
            const float f = pFP[listenCode]->load(), q = juce::jmax(0.5f, pQP[listenCode]->load());
            if (f != lastLbF || q != lastLbQ)
            {
                lb[0].setBandpass(sr, f, q); lb[1].setBandpass(sr, f, q);
                lastLbF = f; lastLbQ = q;
            }
        }
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < nch; ++c)
            {
                const float x = ch[c][i];
                float y = x;
                if (listenCode < kPts)           { y = lb[0].process(c, x); y = 1.6f * lb[1].process(c, y); }
                else if (listenCode == kListenHP) { float z = x; for (int s = 0; s < stages; ++s) z = hp[s].process(c, z); y = x - z; }
                else                              { float z = x; for (int s = 0; s < stages; ++s) z = lp[s].process(c, z); y = x - z; }
                ch[c][i] = y * outG;
            }
            outG += outStep;
        }
    }
    else
    {
        lastLbF = -1.f;
        // ---- точки ----
        bool pActive[kPts], dActive[kPts];
        float gT[kPts], fT[kPts], qT[kPts], thr[kPts];
        for (int i = 0; i < kPts; ++i)
        {
            const bool act = pOnP[i]->load() > 0.5f;
            const bool notch = pNP[i]->load() > 0.5f;
            const bool dyn = act && pDP[i]->load() > 0.5f;
            fT[i] = pFP[i]->load(); qT[i] = pQP[i]->load(); thr[i] = pTP[i]->load();
            gT[i] = pGP[i]->load();
            if (notch) gT[i] = juce::jmin(gT[i], 0.0f);
            dActive[i] = dyn && std::abs(gT[i]) >= 0.05f;
            pActive[i] = act && (dyn ? dActive[i] : std::abs(gT[i]) >= 0.05f);

            if (!act)
            {
                pt[i].reset(); det[i].reset(); dynGain[i] = 0.f; dynEnv[i] = 0.f; ptLastG[i] = 1000.f;
                continue;
            }
            const bool fq = fT[i] != ptLastF[i] || qT[i] != ptLastQ[i];
            if (fq)
            {
                det[i].setBandpass(sr, fT[i], qT[i]);
                if (!dyn) pt[i].setPeaking(sr, fT[i], qT[i], gT[i]);
                ptLastF[i] = fT[i]; ptLastQ[i] = qT[i];
                ptLastG[i] = dyn ? 1000.f : gT[i];
            }
            else if (!dyn && gT[i] != ptLastG[i])
            {
                pt[i].setPeaking(sr, fT[i], qT[i], gT[i]);
                ptLastG[i] = gT[i];
            }
            if (!dActive[i]) { dynGain[i] = 0.f; dynEnv[i] = 0.f; }
        }

        const float atk = std::exp(-1.0f / ((float) sr * juce::jmax(0.1f, pAtk->load()) * 0.001f));
        const float rel = std::exp(-1.0f / ((float) sr * juce::jmax(1.0f, pRel->load()) * 0.001f));

        constexpr int chunk = 32;
        for (int start = 0; start < n; start += chunk)
        {
            const int len = juce::jmin(chunk, n - start);

            for (int d = 0; d < kPts; ++d)
            {
                if (!dActive[d]) continue;
                const float lv = 20.0f * std::log10(juce::jmax(dynEnv[d], 1.0e-6f));
                const float over = lv - thr[d];
                const float sgn = gT[d] > 0 ? 1.0f : -1.0f;
                const float target = over > 0.0f ? sgn * juce::jmin(std::abs(gT[d]), over) : 0.0f;
                dynGain[d] += (target - dynGain[d]) * 0.35f;
                if (std::abs(dynGain[d] - ptLastG[d]) > 0.02f || ptLastG[d] > 999.f)
                {
                    pt[d].setPeaking(sr, fT[d], qT[d], dynGain[d]);
                    ptLastG[d] = dynGain[d];
                }
            }

            for (int i = start; i < start + len; ++i)
            {
                for (int d = 0; d < kPts; ++d)
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
                    if (hpOn) for (int s = 0; s < stages; ++s) x = hp[s].process(c, x);
                    if (lpOn) for (int s = 0; s < stages; ++s) x = lp[s].process(c, x);
                    for (int k = 0; k < kPts; ++k) if (pActive[k]) x = pt[k].process(c, x);
                    ch[c][i] = x * outG;
                }
                outG += outStep;
            }
        }
        for (int d = 0; d < kPts; ++d) dynGainOut[d].store(dynGain[d]);
    }
    outGainSmoothed = outTarget;

    // осциллограмма
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

    const float* chOut[2] = { ch[0], ch[1] };
    outAnalyzer.push(chOut, nch, n);
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
    listen.store(-1);
    for (int i = 0; i < kPts; ++i) setReal(apvts, ids::pOn(i), 0.f);
    setReal(apvts, ids::hpOn, p.hp > 0.f ? 1.f : 0.f);
    if (p.hp > 0.f) setReal(apvts, ids::hpF, p.hp);
    setReal(apvts, ids::lpOn, p.lp > 0.f ? 1.f : 0.f);
    if (p.lp > 0.f) setReal(apvts, ids::lpF, p.lp);
    for (int i = 0; i < 4; ++i)
    {
        const auto& q = p.p[i];
        if (q.f <= 0.f) continue;
        setReal(apvts, ids::pF(i), q.f);
        setReal(apvts, ids::pG(i), q.g);
        setReal(apvts, ids::pQ(i), q.q);
        setReal(apvts, ids::pNotch(i), q.notch ? 1.f : 0.f);
        setReal(apvts, ids::pDyn(i), q.dyn ? 1.f : 0.f);
        setReal(apvts, ids::pThr(i), q.dyn ? q.thr : -30.f);
        setReal(apvts, ids::pOn(i), 1.f);
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
