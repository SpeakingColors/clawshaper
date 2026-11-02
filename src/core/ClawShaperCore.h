// ClawShaper core: pure C++, no JUCE. Same maths as the approved mockup (dsp.js): per-band transient
// shaper (attack / sustain), 1 to 4 bands with all-pass compensated Linkwitz-Riley crossovers,
// lookahead output limiter. The engine is ours; no commercial transient shaper was used as a source.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdint>
#include <cstring>
#include <vector>

namespace cs {

constexpr int NB = 4;
inline double D2L(double db) { return std::pow(10.0, db * 0.05); }
inline double L2D(double x) { return 20.0 * std::log10(std::max(x, 1e-9)); }
template <class T> inline T clampv(T v, T a, T b) { return std::min(b, std::max(a, v)); }

// ---------------------------------------------------------------- filters
struct Biq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double proc(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void lp1(double fs, double f) { double K = std::tan(M_PI * f / fs), n = 1 / (1 + K); b0 = K * n; b1 = K * n; b2 = 0; a1 = (K - 1) * n; a2 = 0; }
    void hp1(double fs, double f) { double K = std::tan(M_PI * f / fs), n = 1 / (1 + K); b0 = n; b1 = -n; b2 = 0; a1 = (K - 1) * n; a2 = 0; }
    void lp(double fs, double f, double q) { double w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al; b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void hp(double fs, double f, double q) { double w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void peak(double fs, double f, double g, double q) { double A = std::pow(10.0, g / 40), w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al / A; b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0; }
};

// Linkwitz-Riley crossover of order N (slope 6N dB/oct). Same as the mockup.
struct XO {
    double lo = 0, hi = 0; int N = -1; bool inv = false;
    struct Spec { int o; double q; };
    std::vector<Biq> lps, hps; std::vector<Spec> specs;
    void shape(int n) {
        if (n == N) return;
        N = n; lps.clear(); hps.clear(); specs.clear(); inv = false;
        if (n == 1) { lps.resize(1); return; }
        int m = n / 2; inv = (m % 2 == 1);
        std::vector<Spec> one;
        if (m % 2 == 1) one.push_back({1, 0});
        for (int k = 1; k <= m / 2; k++) one.push_back({2, 1.0 / (2 * std::sin((2 * k - 1) * M_PI / (2.0 * m)))});
        specs = one; specs.insert(specs.end(), one.begin(), one.end());
        lps.resize(specs.size()); hps.resize(specs.size());
    }
    void set(double fs, double fc, int n) {
        shape(n); fc = std::min(fc, fs * 0.45);
        if (n == 1) { lps[0].lp1(fs, fc); return; }
        for (size_t i = 0; i < specs.size(); i++) {
            if (specs[i].o == 1) { lps[i].lp1(fs, fc); hps[i].hp1(fs, fc); }
            else { lps[i].lp(fs, fc, specs[i].q); hps[i].hp(fs, fc, specs[i].q); }
        }
    }
    void proc(double x) {
        if (N == 1) { double l = lps[0].proc(x); lo = l; hi = x - l; return; }
        double l = x, h = x;
        for (size_t i = 0; i < lps.size(); i++) { l = lps[i].proc(l); h = hps[i].proc(h); }
        lo = l; hi = inv ? -h : h;
    }
    void reset() { for (auto& f : lps) f.reset(); for (auto& f : hps) f.reset(); }
};

// ---------------------------------------------------------------- limiter and delay
struct LL {
    int M = 0, w = 0, la = 0; int64_t h = 0, t = 0, n = 0;
    std::vector<float> bl, br, dv, avg; std::vector<int64_t> dq;
    double sum = 0, g = 1, gr = 0, ol = 0, orr = 0;
    void init(int maxLA) { M = maxLA + 4; bl.assign(M, 0); br.assign(M, 0); dv.assign(M, 1); avg.assign(M, 1); dq.assign(M, 0); la = 0; setLA(std::max(1, maxLA / 2)); }
    void setLA(int l) {
        l = std::max(1, std::min(M - 4, l)); if (l == la) return; la = l;
        std::fill(bl.begin(), bl.end(), 0.f); std::fill(br.begin(), br.end(), 0.f); std::fill(avg.begin(), avg.end(), 1.f);
        sum = la; h = t = 0; n = 0; g = 1; w = 0;
    }
    void proc(double l, double r, double ceil, double kr, bool on) {
        bl[w] = (float)l; br[w] = (float)r;
        double p = std::max(std::fabs(l), std::fabs(r)), gt = (on && p > ceil) ? ceil / p : 1.0;
        int64_t idx = n++;
        while (t > h && dv[(t - 1) % M] >= gt) t--;
        dq[t % M] = idx; dv[t % M] = (float)gt; t++;
        while (dq[h % M] <= idx - la) h++;
        double gm = dv[h % M];
        int ai = (int)(idx % la); sum += gm - avg[ai]; avg[ai] = (float)gm;
        double ga = std::min(1.0, sum / la);
        g = ga < g ? ga : g + (ga - g) * kr;
        int rd = (w - (la - 1) + M) % M; ol = bl[rd] * g; orr = br[rd] * g; w = (w + 1) % M;
        gr = -20 * std::log10(g + 1e-9);
    }
};
struct DL {
    int M = 2, w = 0; std::vector<float> b;
    void init(int maxD) { M = maxD + 2; b.assign(M, 0.f); w = 0; }
    float proc(float x, int d) { b[w] = x; int r = (w - d + M) % M; w = (w + 1) % M; return b[r]; }
    void clear() { std::fill(b.begin(), b.end(), 0.f); }
};

// ---------------------------------------------------------------- parameters (what the plugin hands to the engine)
struct BandP { bool on = true, mute = false, solo = false; float atk = 0, sus = 0; int spd = 1; float thr = -48, max = 12, outG = 0, mix = 100; };
struct ShaperP {
    bool byp = false; int bands = 4, slope = 1; float x[3] { 120, 1200, 6000 };
    float mix = 100, inG = 0, outG = 0; bool limOn = true; float ceil = -0.3f; bool limAuto = true; float limRel = 80, la = 3;
    int listen = 0, boost = 0; BandP b[NB];
};

// ---------------------------------------------------------------- engine
constexpr double kSpeeds[3][4] = { { 0.5, 10, 10, 60 }, { 1, 20, 20, 120 }, { 2, 40, 40, 240 } };
constexpr int kSlopes[8] = { 12, 24, 36, 48, 60, 72, 84, 96 };
constexpr double AK = 1.5, SK = 4, DZ = 1.0, GATE_W = 6, DET_LA_MS = 1.0;
inline double softDz(double x) { double u = x - DZ; return u <= 0 ? 0 : u * u / (u + DZ); }

class Engine {
public:
    static constexpr int kRing = 4096, kHist = 480, kGh = 360;
    double fs = 48000;
    // ---- meters for the editor (written by process; plain floats, benign races)
    struct BandMeter { float lvl = -90, diff = 0, gate = 0, cur = 0; };
    BandMeter band[NB];
    std::atomic<float> prePk[2] { { 0 }, { 0 } }, postPk[2] { { 0 }, { 0 } }, preMs[2] { { 0 }, { 0 } }, postMs[2] { { 0 }, { 0 } }, limGr { 0 };
    float scI[NB][kHist] {}, scO[NB][kHist] {}, grHist[NB][kGh] {};
    std::atomic<int> scPos[NB] { { 0 }, { 0 }, { 0 }, { 0 } }, ghPos { 0 };
    float ringPre[kRing] {}, ringPost[kRing] {}; std::atomic<int> ringW { 0 };

    void prepare(double sampleRate) {
        fs = sampleRate; prepared = false;
        const int maxLA = (int)std::ceil(0.0075 * fs) + 2; dLA = std::max(1, (int)std::lround(DET_LA_MS * 0.001 * fs));
        for (int c = 0; c < NB - 1; c++) for (int ch = 0; ch < 2; ch++) xo[c][ch] = XO();
        for (int b = 0; b < NB; b++) for (int j = 0; j < NB; j++) for (int ch = 0; ch < 2; ch++) ap[b][j][ch] = XO();
        for (int b = 0; b < NB; b++) for (int ch = 0; ch < 2; ch++) bandDL[b][ch].init(dLA + 2);
        for (int ch = 0; ch < 2; ch++) { mixDry[ch].init(dLA + 2); dryTot[ch].init(dLA + maxLA + 8); }
        mLL.init(maxLA);
        colN = std::max(1, (int)std::lround(fs * 0.006));
        for (int b = 0; b < NB; b++) bs[b] = BandState();
        lat = dLA; tpos = 0; ghAcc = 0; prepared = true;
        setParams(P);
    }
    void setParams(const ShaperP& m) {
        P = m; if (!prepared) return;
        nb = std::max(1, std::min(NB, m.bands)); const int N = kSlopes[std::max(0, std::min(7, m.slope))] / 6;
        for (int c = 0; c < nb - 1; c++) for (int ch = 0; ch < 2; ch++) xo[c][ch].set(fs, m.x[c], N);
        for (int b = 2; b < nb; b++) for (int j = 0; j < b - 1; j++) for (int ch = 0; ch < 2; ch++) ap[b][j][ch].set(fs, m.x[j], N);
        double ed[NB + 1]; ed[0] = 20; for (int i = 0; i < nb - 1; i++) ed[i + 1] = m.x[i]; ed[nb] = 20000;
        auto mk = [&](double ms) { return 1 - std::exp(-1 / (std::max(0.01, ms) * 0.001 * fs)); };
        anySolo = false;
        for (int i = 0; i < NB; i++) {
            const auto& b = m.b[i]; const double* sp = kSpeeds[std::max(0, std::min(2, b.spd))];
            const double lo = std::max(30.0, i < nb ? ed[i] : 20.0), hi = i < nb ? ed[i + 1] : 20000.0, fc = std::sqrt(lo * hi), ts = clampv(170.0 / fc, 1.0, 4.0);
            bp[i] = { b.on, b.mute, b.solo, b.atk / 100.0, b.sus / 100.0, (double)b.thr, (double)b.max, D2L(b.outG), b.mix / 100.0, mk(sp[0] * ts), mk(sp[1] * ts), mk(sp[2] * ts), mk(sp[3] * ts) };
            if (i < nb && b.solo) anySolo = true;
        }
        kGa = mk(0.5); kGr = mk(5); kW = mk(1);
        gMix = m.mix / 100.0; gIn = D2L(m.inG); gOut = D2L(m.outG); ceilL = D2L(m.ceil); kLr = 1 - std::exp(-1 / (std::max(1.0f, m.limRel) * 0.001 * fs));
        LA = std::max(1, (int)std::lround(m.la * 0.001 * fs)); if (LA != mLL.la) mLL.setLA(LA);
        lat = dLA + LA - 1; boostK = m.boost == 2 ? 4.0 : m.boost == 1 ? 2.0 : 1.0;
    }
    int latencySamples() const { return lat; }
    void reset() {
        for (auto& a : xo) for (auto& x : a) { for (auto& f : x.lps) f.reset(); for (auto& f : x.hps) f.reset(); }
        for (auto& a : ap) for (auto& p : a) for (auto& x : p) { for (auto& f : x.lps) f.reset(); for (auto& f : x.hps) f.reset(); }
        for (auto& s : bs) { s.f = s.s = s.cur = 0; s.wa = 0.5; }
        for (auto& a : bandDL) for (auto& d : a) d.clear();
        for (auto& d : mixDry) d.clear();
        for (auto& d : dryTot) d.clear();
        mLL.la = 0; mLL.setLA(LA);
    }
    void process(float* L, float* R, int n) {
        if (!prepared) return;
        double preP = 0, postP = 0;
        for (int i = 0; i < n; i++) {
            double xl = L[i], xr = R[i];
            const float dl = dryTot[0].proc((float)xl, lat), dr = dryTot[1].proc((float)xr, lat);   // bypass copy, delayed by the latency
            xl *= gIn; xr *= gIn;
            const double preDb = std::max(std::fabs(xl), std::fabs(xr)); pushRing(ringPre, (float)((xl + xr) * 0.5));
            double bl[NB], br[NB];
            if (nb == 1) { bl[0] = xl; br[0] = xr; }
            else {
                double sl = xl, sr = xr;
                for (int c = nb - 2; c >= 0; c--) {
                    auto& a = xo[c][0]; auto& b = xo[c][1]; a.proc(sl); b.proc(sr); double hl = a.hi, hr = b.hi;
                    for (int j = 0; j < c; j++) { auto& pa = ap[c + 1][j][0]; auto& pb = ap[c + 1][j][1]; pa.proc(hl); pb.proc(hr); hl = pa.lo + pa.hi; hr = pb.lo + pb.hi; }
                    bl[c + 1] = hl; br[c + 1] = hr; sl = a.lo; sr = b.lo;
                }
                bl[0] = sl; br[0] = sr;
            }
            double wl = 0, wr = 0;
            for (int b = 0; b < nb; b++) {
                const auto& p = bp[b]; auto& S = bs[b]; const double l = bl[b], r = br[b];
                const double lv = std::max(std::fabs(l), std::fabs(r));
                S.f += (lv - S.f) * (lv > S.f ? p.kfa : p.kfr); S.s += (lv - S.s) * (lv > S.s ? p.ksa : p.ksr);
                const double fdb = 20 * std::log10(S.f + 1e-6), sdb = 20 * std::log10(S.s + 1e-6), diff = fdb - sdb;
                S.lvl = fdb; S.diff = diff;
                double tg = 0;
                if (p.on) {
                    const double gate = clampv((fdb - p.thr) / GATE_W, 0.0, 1.0); S.gate = gate;
                    tg = (diff > 0 ? p.atk * AK * boostK * softDz(diff) : p.sus * SK * boostK * softDz(-diff)) * gate;
                    tg = p.lim > 0.001 ? p.lim * std::tanh(tg / p.lim) : 0;
                }
                S.cur += (tg - S.cur) * (std::fabs(tg) > std::fabs(S.cur) ? kGa : kGr);
                const double g = std::exp(S.cur * 0.1151293);
                const double wa = clampv(0.5 + diff / 6, 0.0, 1.0); S.wa += (wa - S.wa) * kW;
                const double dBl = bandDL[b][0].proc((float)l, dLA), dBr = bandDL[b][1].proc((float)r, dLA);
                double al = dBl * (1 + (g - 1) * p.mix), ar = dBr * (1 + (g - 1) * p.mix);
                al *= p.outG; ar *= p.outG;
                if (P.listen == 1) { al *= S.wa; ar *= S.wa; } else if (P.listen == 2) { al *= 1 - S.wa; ar *= 1 - S.wa; }
                const bool audible = !p.mute && (!anySolo || p.solo);
                S.aI = std::max(S.aI, std::max(std::fabs(dBl), std::fabs(dBr)));
                S.aO = std::max(S.aO, audible ? std::max(std::fabs(al), std::fabs(ar)) : 0.0);
                if (std::fabs(S.cur) > std::fabs(S.ext)) S.ext = S.cur;
                if (++S.aN >= colN) { const int k = scPos[b].load(std::memory_order_relaxed); scI[b][k] = (float)S.aI; scO[b][k] = (float)S.aO; scPos[b].store((k + 1) % kHist, std::memory_order_release); S.aI = S.aO = 0; S.aN = 0; }
                if (audible) { wl += al; wr += ar; }
            }
            const double tdl = mixDry[0].proc((float)xl, dLA), tdr = mixDry[1].proc((float)xr, dLA);
            double ol, orr;
            if (P.listen == 0) { ol = tdl * (1 - gMix) + wl * gMix; orr = tdr * (1 - gMix) + wr * gMix; } else { ol = wl; orr = wr; }
            ol *= gOut; orr *= gOut;
            const double kr = P.limAuto ? 1 - std::exp(-1 / ((30 + 10 * std::min(mLL.gr, 20.0)) * 0.001 * fs)) : kLr;
            mLL.proc(ol, orr, ceilL, kr, P.limOn); ol = mLL.ol; orr = mLL.orr; limGrNow = P.limOn ? mLL.gr : 0;
            if (P.byp) { ol = dl; orr = dr; }
            if (!std::isfinite(ol) || !std::isfinite(orr)) { ol = orr = 0; reset(); }
            pushRing(ringPost, (float)((ol + orr) * 0.5)); ringW.store((ringW.load(std::memory_order_relaxed) + 1) & (kRing - 1), std::memory_order_release);
            L[i] = (float)ol; R[i] = (float)orr;
            postP = std::max(postP, std::max(std::fabs(ol), std::fabs(orr))); preP = std::max(preP, preDb);
            pr[0] += (xl * xl - pr[0]) * 0.00015; pr[1] += (xr * xr - pr[1]) * 0.00015; qr[0] += (ol * ol - qr[0]) * 0.00015; qr[1] += (orr * orr - qr[1]) * 0.00015;
            pl[0] = std::max(pl[0], std::fabs(xl)); pl[1] = std::max(pl[1], std::fabs(xr)); ql[0] = std::max(ql[0], std::fabs(ol)); ql[1] = std::max(ql[1], std::fabs(orr));
        }
        tpos += n; const double dec = std::pow(0.9, n / 512.0);
        for (int c = 0; c < 2; c++) {
            const float a = (float)std::max(pl[c], hpPre[c] * dec), b2 = (float)std::max(ql[c], hpPost[c] * dec); hpPre[c] = a; hpPost[c] = b2; pl[c] = ql[c] = 0;
            prePk[c].store(a); postPk[c].store(b2); preMs[c].store((float)pr[c]); postMs[c].store((float)qr[c]);
        }
        limGr.store((float)limGrNow);
        for (int b = 0; b < NB; b++) { band[b].lvl = (float)bs[b].lvl; band[b].diff = (float)bs[b].diff; band[b].gate = (float)bs[b].gate; band[b].cur = (float)bs[b].cur; }
        ghAcc += n;
        if (ghAcc >= fs / 60) { ghAcc = 0; const int k = ghPos.load(std::memory_order_relaxed); for (int b = 0; b < NB; b++) { grHist[b][k] = (float)bs[b].ext; bs[b].ext = bs[b].cur; } ghPos.store((k + 1) % kGh, std::memory_order_release); }
    }

private:
    struct BandState { double f = 0, s = 0, cur = 0, wa = 0.5, lvl = -90, diff = 0, gate = 0, aI = 0, aO = 0, ext = 0; int aN = 0; };
    struct BP { bool on, mute, solo; double atk, sus, thr, lim, outG, mix, kfa, kfr, ksa, ksr; };
    void pushRing(float* r, float v) { const int w = ringW.load(std::memory_order_relaxed); r[w] = v; }
    // the ring write index advances once per sample, after both rings were written
    ShaperP P; bool prepared = false; int nb = 4, dLA = 48, LA = 144, lat = 192, colN = 288;
    XO xo[NB - 1][2], ap[NB][NB][2]; DL bandDL[NB][2], mixDry[2], dryTot[2]; LL mLL;
    BandState bs[NB]; BP bp[NB] {}; bool anySolo = false;
    double kGa = 0.1, kGr = 0.01, kW = 0.02, gMix = 1, gIn = 1, gOut = 1, ceilL = 0.97, kLr = 0.001, boostK = 1, limGrNow = 0, tpos = 0, ghAcc = 0;
    double pl[2] {}, ql[2] {}, pr[2] {}, qr[2] {}, hpPre[2] {}, hpPost[2] {};
};

} // namespace cs
