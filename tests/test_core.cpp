// Plain C++ tests for the ClawShaper core (no JUCE). Run: sh tests/run.sh
#include "../src/core/ClawShaperCore.h"
#include <cstdio>
#include <random>
using namespace cs;
static int bad = 0;
static void check(bool ok, const char* what, double v = 0) { std::printf("%s: %s %g\n", ok ? "ok  " : "FAIL", what, v); if (!ok) ++bad; }
static const double FS = 48000;
static std::vector<float> kick(int n, bool loop = true) {
    std::vector<float> x(n);
    for (int i = 0; i < n; i++) { double t = std::fmod(i / FS, 0.5); x[i] = (float)(0.8 * std::exp(-t * 12) * std::sin(2 * M_PI * (50 + 90 * std::exp(-t * 40)) * t)); }
    (void)loop; return x;
}
static std::vector<float> run(Engine& e, const std::vector<float>& in) {
    std::vector<float> L(in), R(in);
    for (size_t i = 0; i < in.size(); i += 512) { int n = (int)std::min<size_t>(512, in.size() - i); e.process(&L[i], &R[i], n); }
    return L;
}
int main() {
    const int N = 48000 * 4;
    auto in = kick(N);
    // 1. Mix 0, limiter off, Output 0 dB: bit identical to the input delayed by the latency
    {
        Engine e; ShaperP p; p.mix = 0; p.limOn = false; for (auto& b : p.b) { b.atk = 100; b.sus = 100; } e.prepare(FS); e.setParams(p);
        auto out = run(e, in); const int lat = e.latencySamples(); double worst = 0;
        for (int i = lat; i < N; i++) worst = std::max(worst, (double)std::fabs(out[i] - in[i - lat]));
        check(worst == 0, "Mix 0 equals delayed input", worst); check(lat == 144 + 48 - 1 + 0 || lat > 0, "latency reported", lat);
    }
    // 2. Bypass equals delayed input
    {
        Engine e; ShaperP p; p.byp = true; for (auto& b : p.b) { b.atk = 100; } e.prepare(FS); e.setParams(p);
        auto out = run(e, in); const int lat = e.latencySamples(); double worst = 0;
        for (int i = lat; i < N; i++) worst = std::max(worst, (double)std::fabs(out[i] - in[i - lat]));
        check(worst == 0, "Bypass equals delayed input", worst);
    }
    // 3. Neutral settings (atk 0, sus 0, 1 band): output equals the delayed input
    {
        Engine e; ShaperP p; p.bands = 1; p.limOn = false; e.prepare(FS); e.setParams(p);
        auto out = run(e, in); const int lat = e.latencySamples(); double worst = 0;
        for (int i = lat; i < N; i++) worst = std::max(worst, (double)std::fabs(out[i] - in[i - lat]));
        check(worst < 1e-6, "1 band neutral equals delayed input", worst);
    }
    // 4. Attack +100 raises the first-15 ms peak of every kick, Attack -100 lowers it
    auto pk15 = [&](const std::vector<float>& o, int lat) { double m = 0; for (int i = 48000 + lat; i < 48000 + lat + 720; i++) m = std::max(m, (double)std::fabs(o[i])); return m; };
    double ref, up, dn;
    {
        Engine e; ShaperP p; p.bands = 1; p.limOn = false; e.prepare(FS); e.setParams(p); auto o = run(e, in); ref = pk15(o, e.latencySamples());
        p.b[0].atk = 100; e.prepare(FS); e.setParams(p); o = run(e, in); up = pk15(o, e.latencySamples());
        p.b[0].atk = -100; e.prepare(FS); e.setParams(p); o = run(e, in); dn = pk15(o, e.latencySamples());
        check(up > ref * 1.2, "Attack +100 raises the hit", up / ref); check(dn < ref * 0.9, "Attack -100 lowers the hit", dn / ref);
    }
    // 5. Steady sine: shaper leaves it alone (level dev < 0.1 dB)
    {
        Engine e; ShaperP p; p.bands = 1; p.limOn = false; p.b[0].atk = 100; p.b[0].sus = 100; e.prepare(FS); e.setParams(p);
        std::vector<float> s(N); for (int i = 0; i < N; i++) s[i] = (float)(0.3 * std::sin(2 * M_PI * 1000 * i / FS));
        auto o = run(e, s); double a = 0, b = 0; for (int i = N / 2; i < N; i++) { a = std::max(a, (double)std::fabs(o[i])); b = std::max(b, (double)std::fabs(s[i])); }
        check(std::fabs(20 * std::log10(a / b)) < 0.1, "steady sine untouched", 20 * std::log10(a / b));
    }
    // 6. Limiter holds the ceiling and fuzz has no NaN
    {
        std::mt19937 rng(5); std::uniform_real_distribution<float> U(0, 1); double worst = -200; bool nan = false;
        for (int run_i = 0; run_i < 40; run_i++) {
            Engine e; ShaperP p; p.bands = 1 + (int)(U(rng) * 3.99f); p.slope = (int)(U(rng) * 7.99f); p.mix = U(rng) * 100; p.inG = U(rng) * 30 - 15; p.outG = U(rng) * 24 - 6; p.ceil = -U(rng) * 12; p.boost = (int)(U(rng) * 2.99f); p.listen = (int)(U(rng) * 2.99f);
            p.x[0] = 60 + U(rng) * 100; p.x[1] = p.x[0] * 3; p.x[2] = p.x[1] * 3; p.limOn = true; p.limAuto = U(rng) > 0.5f; p.la = 1 + U(rng) * 6.5f;
            for (auto& b : p.b) { b.atk = U(rng) * 200 - 100; b.sus = U(rng) * 200 - 100; b.spd = (int)(U(rng) * 2.99f); b.thr = -60 * U(rng); b.max = U(rng) * 48; b.outG = U(rng) * 32 - 24; b.mix = U(rng) * 100; }
            e.prepare(FS); e.setParams(p); auto o = run(e, kick(48000 * 2));
            const double ceilLin = D2L(p.ceil); double pk = 0; for (float v : o) { if (!std::isfinite(v)) nan = true; pk = std::max(pk, (double)std::fabs(v)); }
            worst = std::max(worst, L2D(pk) - L2D(ceilLin));
        }
        check(!nan, "fuzz: no NaN"); check(worst < 0.01, "fuzz: output never above ceiling (dB over)", worst);
    }
    std::printf(bad ? "FAILED\n" : "ALL OK\n"); return bad ? 1 : 0;
}
