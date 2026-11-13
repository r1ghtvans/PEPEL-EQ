#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>

// Анализатор спектра. Вызывается из аудио-потока, результаты читаются из интерфейса через atomic.
//  - 48 полос "уровня" для городского силуэта;
//  - "грязь" dirt (жёсткость звука 2-9 кГц + выпирающие пики), сглаживание ~0.35 с;
//  - поиск шумящих частот: усреднённый спектр + устойчивые узкие пики.
class Analyzer
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int N = 1 << fftOrder;   // 4096
    static constexpr int hop = N / 2;
    static constexpr int K = N / 2;           // число бинов
    static constexpr int numBands = 48;
    static constexpr int maxFound = 4;
    static constexpr int numSpec = 160;   // точки спектра 20 Гц .. 20 кГц (лог)

    Analyzer() : fft(fftOrder)
    {
        for (int i = 0; i < N; ++i)
            window[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
        for (auto& b : bands) b.store(0.0f);
        for (auto& f : foundFreq) f.store(0.0f);
        for (auto& f : foundProm) f.store(0.0f);
        for (auto& v : spec) v.store(-120.0f);
    }

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        ring.fill(0.0f);
        wr = 0; hopCount = 0;
        clearSearch();
        dirtSm = 0.0f;
        dirt.store(0.0f);
        // границы полос (лог-шкала 30 Гц .. 16 кГц)
        for (int b = 0; b < numBands; ++b)
        {
            double f0 = 30.0 * std::pow(16000.0 / 30.0, (double) b / numBands);
            double f1 = 30.0 * std::pow(16000.0 / 30.0, (double) (b + 1) / numBands);
            bandLo[b] = juce::jlimit(1, K - 1, (int) std::floor(f0 * N / sr));
            bandHi[b] = juce::jlimit(bandLo[b], K - 1, (int) std::floor(f1 * N / sr));
        }
        for (int i = 0; i < numSpec; ++i)
        {
            const double f  = 20.0 * std::pow(1000.0, (double) i / (numSpec - 1));
            const double fL = 20.0 * std::pow(1000.0, ((double) i - 0.5) / (numSpec - 1));
            const double fH = 20.0 * std::pow(1000.0, ((double) i + 0.5) / (numSpec - 1));
            specPos[i] = (float) (f * N / sr);
            specLo[i] = juce::jlimit(1, K - 2, (int) std::floor(fL * N / sr));
            specHi[i] = juce::jlimit(specLo[i], K - 2, (int) std::floor(fH * N / sr));
        }
        kHarshLo = juce::jlimit(1, K - 1, (int) (2000.0 * N / sr));
        kHarshHi = juce::jlimit(1, K - 1, (int) (9000.0 * N / sr));
    }

    void push(const float* const* chans, int numCh, int n)
    {
        if (numCh <= 0) return;
        const float inv = 1.0f / (float) numCh;
        for (int i = 0; i < n; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < numCh; ++c) m += chans[c][i];
            ring[(size_t) wr] = m * inv;
            wr = (wr + 1) & (N - 1);
            if (++hopCount >= hop) { hopCount = 0; frame(); }
        }
    }

    // ----- результаты (читать из UI) -----
    std::atomic<float> dirt { 0.0f };
    std::array<std::atomic<float>, numBands> bands;
    std::array<std::atomic<float>, maxFound> foundFreq, foundProm;
    std::atomic<int> foundCount { 0 };
    std::atomic<float> searchSeconds { 0.0f };
    std::array<std::atomic<float>, numSpec> spec;   // дБ (отн. синуса 0 дБ)

    // ----- управление поиском -----
    std::atomic<bool> searching { false };
    std::atomic<bool> clearRequest { false };
    bool computeDirt = false;       // включить расчёт dirt (только для выходного анализатора)
    bool doSearch = false;          // разрешить накопление для поиска (только для входного)

private:
    void clearSearch()
    {
        avgPow.fill(0.0);
        frames = 0;
        foundCount.store(0);
        searchSeconds.store(0.0f);
    }

    void frame()
    {
        // окно по последним N сэмплам
        for (int i = 0; i < N; ++i)
            fftData[(size_t) i] = ring[(size_t) ((wr + i) & (N - 1))] * window[(size_t) i];
        for (int i = N; i < 2 * N; ++i) fftData[(size_t) i] = 0.0f;
        fft.performFrequencyOnlyForwardTransform(fftData.data(), true);

        // нормировка: синус амплитудой A даёт |X| = A*N/4
        const float norm = 4.0f / (float) N;
        double tot = 0.0, harsh = 0.0;
        for (int k = 1; k < K; ++k)
        {
            float m = fftData[(size_t) k] * norm;
            pw[(size_t) k] = (double) m * (double) m;
            tot += (double) fftData[(size_t) k] * (double) fftData[(size_t) k];
        }
        for (int k = kHarshLo; k <= kHarshHi; ++k)
            harsh += (double) fftData[(size_t) k] * (double) fftData[(size_t) k];

        // полосы уровня
        for (int b = 0; b < numBands; ++b)
        {
            double mx = 0.0;
            for (int k = bandLo[b]; k <= bandHi[b]; ++k) mx = juce::jmax(mx, pw[(size_t) k]);
            float db = 10.0f * std::log10((float) mx + 1.0e-12f);
            bands[(size_t) b].store(juce::jlimit(0.0f, 1.0f, (db + 70.0f) / 50.0f));
        }

        // спектр для экрана
        for (int i = 0; i < numSpec; ++i)
        {
            double m;
            if (specHi[i] > specLo[i])
            {
                m = 0.0;
                for (int k = specLo[i]; k <= specHi[i]; ++k) m = juce::jmax(m, pw[(size_t) k]);
            }
            else
            {
                const float p = specPos[i];
                const int k = juce::jlimit(1, K - 2, (int) p);
                const double fr = juce::jlimit(0.0f, 1.0f, p - (float) k);
                m = pw[(size_t) k] * (1.0 - fr) + pw[(size_t) k + 1] * fr;
            }
            spec[(size_t) i].store(10.0f * std::log10((float) m + 1.0e-12f));
        }

        // средняя мощность (дБ отн. полной шкалы): msq = 5.33 * tot / N^2
        const double msq = 5.3333 * tot / ((double) N * (double) N);
        const float rmsDb = 10.0f * std::log10((float) msq + 1.0e-12f);
        const bool audible = rmsDb > -72.0f;

        if (computeDirt) updateDirt(tot, harsh, audible);
        if (doSearch)   updateSearch(audible);
    }

    // ---- "грязь" ----
    void updateDirt(double tot, double harsh, bool audible)
    {
        float raw = 0.0f;
        if (audible && tot > 0.0)
        {
            const float ratio = (float) (harsh / tot);
            // заметность пиков в зоне 1.5-10 кГц
            buildDb(true);
            float prom = 0.0f;
            const int a = juce::jlimit(8, K - 8, (int) (1500.0 * N / sr));
            const int b = juce::jlimit(8, K - 8, (int) (10000.0 * N / sr));
            for (int k = a; k <= b; ++k)
                prom = juce::jmax(prom, prominenceAt(k));
            const float a1 = juce::jlimit(0.0f, 1.0f, (ratio - 0.10f) / 0.40f);
            const float a2 = juce::jlimit(0.0f, 1.0f, (prom - 12.0f) / 20.0f);
            raw = 0.65f * a1 + 0.35f * a2;
        }
        const float dt = (float) hop / (float) sr;
        const float alpha = 1.0f - std::exp(-dt / 0.35f);
        dirtSm += (raw - dirtSm) * alpha;
        dirt.store(juce::jlimit(0.0f, 1.0f, dirtSm));
    }

    // ---- поиск шума ----
    void updateSearch(bool audible)
    {
        if (clearRequest.exchange(false)) clearSearch();
        if (!searching.load()) return;
        if (audible)
        {
            for (int k = 1; k < K; ++k) avgPow[(size_t) k] += pw[(size_t) k];
            ++frames;
            searchSeconds.store((float) ((double) frames * hop / sr));
            if (frames >= 6 && (frames % 6) == 0) findPeaks();
        }
    }

    void buildDb(bool single)
    {
        // dB-массив и префиксные суммы (single: из текущего кадра, иначе из средней мощности)
        pre[0] = 0.0;
        for (int k = 0; k < K; ++k)
        {
            double p = single ? pw[(size_t) k] : (frames > 0 ? avgPow[(size_t) k] / (double) frames : 0.0);
            double d = 10.0 * std::log10(p + 1.0e-14);
            db[(size_t) k] = (float) d;
            pre[(size_t) k + 1] = pre[(size_t) k] + d;
        }
    }

    float prominenceAt(int k) const
    {
        const int hw = juce::jmax(6, (int) (k * 0.2));
        const int lo = juce::jmax(1, k - hw), hi = juce::jmin(K - 1, k + hw);
        const int elo = juce::jmax(1, k - 3), ehi = juce::jmin(K - 1, k + 3);
        const int cnt = (hi - lo + 1) - (ehi - elo + 1);
        if (cnt <= 0) return 0.0f;
        const double sumAll = pre[(size_t) hi + 1] - pre[(size_t) lo];
        const double sumEx  = pre[(size_t) ehi + 1] - pre[(size_t) elo];
        const float base = (float) ((sumAll - sumEx) / (double) cnt);
        return db[(size_t) k] - base;
    }

    void findPeaks()
    {
        buildDb(false);
        int nc = 0;
        const int kMin = juce::jmax(3, (int) (40.0 * N / sr));
        const int kMax = juce::jmin(K - 4, (int) (16000.0 * N / sr));
        for (int k = kMin; k <= kMax && nc < (int) cand.size(); ++k)
        {
            if (db[(size_t) k] > db[(size_t) k - 1] && db[(size_t) k] >= db[(size_t) k + 1])
            {
                float p = prominenceAt(k);
                if (p >= 8.0f) { cand[(size_t) nc].k = k; cand[(size_t) nc].prom = p; ++nc; }
            }
        }
        std::sort(cand.begin(), cand.begin() + nc, [](const Cand2& a, const Cand2& b) { return a.prom > b.prom; });
        int found = 0;
        float chosen[maxFound] = {};
        for (int i = 0; i < nc && found < maxFound; ++i)
        {
            const int k = cand[(size_t) i].k;
            const float d0 = db[(size_t) k - 1], d1 = db[(size_t) k], d2 = db[(size_t) k + 1];
            const float den = d0 - 2.0f * d1 + d2;
            const float delta = std::abs(den) > 1.0e-6f ? juce::jlimit(-0.5f, 0.5f, 0.5f * (d0 - d2) / den) : 0.0f;
            const float f = ((float) k + delta) * (float) sr / (float) N;
            bool tooClose = false;
            for (int j = 0; j < found; ++j)
                if (std::abs(std::log2(f / chosen[j])) < 1.0f / 6.0f) tooClose = true;
            if (tooClose) continue;
            chosen[found] = f;
            foundFreq[(size_t) found].store(f);
            foundProm[(size_t) found].store(cand[(size_t) i].prom);
            ++found;
        }
        foundCount.store(found);
    }

    struct Cand2 { int k; float prom; };

    juce::dsp::FFT fft;
    double sr = 44100.0;
    std::array<float, N> window {};
    std::array<float, N> ring {};
    std::array<float, 2 * N> fftData {};
    std::array<double, K + 1> pw {}, avgPow {}, pre {};
    std::array<float, K + 1> db {};
    std::array<Cand2, 512> cand {};
    int wr = 0, hopCount = 0;
    int frames = 0;
    float dirtSm = 0.0f;
    int bandLo[numBands] = {}, bandHi[numBands] = {};
    int kHarshLo = 1, kHarshHi = 1;
    float specPos[numSpec] = {};
    int specLo[numSpec] = {}, specHi[numSpec] = {};
};
