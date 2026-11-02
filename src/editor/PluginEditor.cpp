#include "PluginEditor.h"
#include "Layout.h"
#include "Theme.h"
#include "Presets.h"
#include "../plugin/ParamMap.h"
#include <juce_dsp/juce_dsp.h>

using namespace juce;
using theme::FontKind;

namespace
{
inline Colour argb (unsigned v) { return Colour ((uint32) v); }
inline Rectangle<float> R (const layout::Rc& r) { return { r.x, r.y, r.w, r.h }; }
inline float clampf (float v, float a, float b) { return jlimit (a, b, v); }
inline String U (const char* c) { return String::fromUTF8 (c); }
inline float l2d (float x) { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }
const Colour BC[4] { Colour (0xff6CC9F5), Colour (0xff02EBBA), Colour (0xffFFD100), Colour (0xffFF7BB1) };
constexpr float WH = 680.0f;   // plugin window height (the 40 px preview rig of the mockup is not part of the plugin)
constexpr float RIGH = 40.0f;

// ---------------------------------------------------------------- number formats (same as the mockup)
String fHz (float v)
{
    if (v >= 1000.0f) { String s (v / 1000.0f, v >= 10000.0f ? 1 : 2); if (s.contains (".")) s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd ("."); return s + " kHz"; }
    return (v >= 100.0f ? String (roundToInt (v)) : String (v, 1).replace (".0", "")) + " Hz";
}
String fDb (float v, int dp = 1) { return String (v > 0.049f ? "+" : "") + String (v, dp) + " dB"; }
String fMs (float ms)
{
    if (ms >= 1000.0f) return String (ms / 1000.0f, 2) + " s";
    if (ms >= 100.0f) return String (roundToInt (ms)) + " ms";
    if (ms >= 10.0f) return String (ms, 1) + " ms";
    return String (ms, 2) + " ms";
}
String fPct (float v) { return String (roundToInt (v)) + "%"; }
String fSgn (float v) { return String (v > 0.5f ? "+" : "") + String (roundToInt (v)) + "%"; }

// ---------------------------------------------------------------- wood cheeks and logo
struct Mul32
{
    uint32_t a;
    double next() { a += 0x6D2B79F5u; uint32_t t = a ^ (a >> 15); t *= (1u | a); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return (double) (t ^ (t >> 14)) / 4294967296.0; }
};
Image makeWood (uint32_t seed, bool edgeRight)
{
    const int w = 104, h = 1280;
    Image img (Image::ARGB, w, h, true);
    Graphics g (img);
    Mul32 r { seed };
    g.fillAll (Colour (0xff3d2210));
    for (int x = 0; x < w; ++x)
    {
        const double a = r.next();
        g.setColour (Colour::fromFloatRGBA (123 / 255.0f, 74 / 255.0f, 38 / 255.0f, (float) (0.40 * (0.4 + 0.6 * a))));
        g.fillRect (x, 0, 1, h);
        if (r.next() < 0.35)
        {
            const float al = (float) (0.75 * r.next());
            const double y0 = r.next() * h, len = h * (0.2 + 0.6 * r.next());
            g.setColour (Colour::fromFloatRGBA (90 / 255.0f, 52 / 255.0f, 25 / 255.0f, al));
            g.fillRect ((float) x, (float) y0, 1.0f, (float) len);
        }
        if (r.next() < 0.12) { g.setColour (Colours::black.withAlpha (0.35f)); g.fillRect (x, 0, 1, h); }
    }
    g.setColour (Colours::black);
    g.fillRect (edgeRight ? 0 : w - 3, 0, 3, h);
    return img;
}
std::unique_ptr<Drawable> makeLogo()
{
    static const char* svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64"><path d="M8 6 L24 22 H40 L56 6 L60 36 Q60 58 32 60 Q4 58 4 36 Z" fill="#FF7BB1"/><path d="M12 14 L21 23 H14 Z M52 14 L43 23 H50 Z" fill="#FBBDD0"/><ellipse cx="22" cy="36" rx="4" ry="5" fill="#16171a"/><ellipse cx="42" cy="36" rx="4" ry="5" fill="#16171a"/><path d="M29 44 H35 L32 48 Z" fill="#16171a"/><path d="M32 48 Q28 53 24 50 M32 48 Q36 53 40 50" stroke="#16171a" stroke-width="2" fill="none"/><path d="M2 40 H14 M2 46 H14 M62 40 H50 M62 46 H50" stroke="#FBBDD0" stroke-width="1.6"/></svg>)";
    if (auto xml = XmlDocument::parse (svg)) return Drawable::createFromSVG (*xml);
    return {};
}
constexpr float BX[4] = { 60, 352, 643, 935 };
} // namespace

// ================================================================================================
struct ClawShaperEditor::Impl
{
    enum Kind { Knob, Toggle, Power, Radio, Seg, Select, Btn, Cv };
    struct Item
    {
        int kind = 0; layout::Rc r {}, labR {}, valR {}, knobR {}; String tip, pid, text, cls, label, action; int idx = -1, band = -1, cv = -1, off = 0;
        Colour col; StringArray opts;
    };

    ClawShaperEditor& ed; ClawShaperProcessor& proc;
    char look = 'C';
    bool tips = true;
    std::vector<Item> items;
    Image woodL, woodR;
    std::unique_ptr<Drawable> logo;
    int hot = -1, tipItem = -1, tipTicks = 0; Point<float> tipPos;
    String tipText;
    int dragItem = -1; double dragStartNorm = 0; Point<float> dragStart;
    String dragKind; int dragBand = -1;
    bool clipPre[2] { false, false }, clipPost[2] { false, false };
    dsp::FFT fft { 12 };
    std::array<float, 8192> work {};
    std::array<float, 2048> dbPre {}, dbPost {};
    std::array<float, 4096> win {};
    String toastText; int toastTicks = 0;
    String presetName;

    Impl (ClawShaperEditor& e, ClawShaperProcessor& p) : ed (e), proc (p)
    {
        woodL = makeWood (0x1234abcdu, false); woodR = makeWood (0x9876fedcu, true);
        logo = makeLogo();
        for (int i = 0; i < 4096; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * MathConstants<float>::pi * (float) i / 4096.0f);
        dbPre.fill (-120.0f); dbPost.fill (-120.0f);
        const auto lk = proc.getSetting (setting::look, "C").toString();
        if (lk.isNotEmpty()) look = lk[0];
        tips = (bool) proc.getSetting (setting::tips, true);
        presetName = proc.getSetting (setting::preset, "").toString();
        build();
    }

    const theme::Look& T() const { return theme::look (look); }

    // ---------------------------------------------------------------- parameter access
    static const cs::PDef* defOf (const String& id)
    {
        for (const auto& d : cs::kParams) if (id == d.id) return &d;
        for (const auto& d : cs::kUi) if (id == d.id) return &d;
        return nullptr;
    }
    static bool isHost (const String& id) { for (const auto& d : cs::kParams) if (id == d.id) return true; return false; }
    float get (const String& id) const
    {
        if (isHost (id)) if (auto* p = proc.apvts.getParameter (id)) return p->convertFrom0to1 (p->getValue());
        const auto* d = defOf (id);
        return (float) (double) proc.getSetting (("ui." + id).toRawUTF8(), d ? d->df : 0.0f);
    }
    void set (const String& id, float v)
    {
        const auto* d = defOf (id); if (! d) return;
        v = clampf (v, d->mn, d->mx); if (d->isInt) v = std::round (v);
        if (isHost (id)) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }
        else proc.setSetting (("ui." + id).toRawUTF8(), v);
        ed.repaint();
    }
    void begin (const String& id) { if (isHost (id)) if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture(); }
    void end (const String& id) { if (isHost (id)) if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture(); }
    void setUser (const String& id, float v) { begin (id); set (id, v); end (id); }
    double posOf (const String& id, float v) const
    {
        const auto* d = defOf (id); if (! d) return 0;
        return d->log ? std::log (v / d->mn) / std::log (d->mx / d->mn) : (v - d->mn) / (d->mx - d->mn);
    }
    float valOf (const String& id, double t) const
    {
        const auto* d = defOf (id); if (! d) return 0;
        t = jlimit (0.0, 1.0, t);
        float v = d->log ? d->mn * (float) std::pow (d->mx / d->mn, t) : d->mn + (float) t * (d->mx - d->mn);
        return d->isInt ? std::round (v) : v;
    }
    int bands() const { return (int) get ("m.bands"); }
    bool bandActive (int b) const { return b < 0 || b < bands(); }

    // ---------------------------------------------------------------- value text on the knobs
    String fmt (const String& id, float v) const
    {
        const String k = id.fromFirstOccurrenceOf (".", false, false);
        if (k == "inG" || k == "outG") return fDb (v);
        if (k == "ceil") return String (v, 1);
        if (k == "mix") return fPct (v);
        if (k == "atk" || k == "sus") return fSgn (v);
        if (k == "max") return String (juce::CharPointer_UTF8 ("\xc2\xb1")) + String (roundToInt (v));
        if (k == "lRel") return get ("m.lAuto") > 0.5f ? String ("AUTO") : fMs (v);
        if (k == "la") return String (v, 1) + " ms";
        if (k == "zoom") return String (roundToInt (1 + v * 7)) + "x";
        return String (v, 1);
    }

    // ---------------------------------------------------------------- items
    void build()
    {
        items.clear();
        for (const auto& c : layout::ctls0)
        {
            Item it; it.kind = c.kind; it.r = c.r; it.labR = c.labR; it.valR = c.valR; it.knobR = c.knobR; it.tip = U (c.tip); it.pid = c.pid; it.idx = c.idx;
            it.text = U (c.text); it.cls = c.cls; it.label = U (c.label); it.col = argb (c.color | 0xff000000u);
            if (c.kind == Select) { it.opts = StringArray::fromTokens (String (c.opts).removeCharacters ("[]\""), ",", ""); it.off = c.off; }
            if (it.pid.startsWithChar ('b') && it.pid[1] >= '0' && it.pid[1] <= '3') it.band = it.pid[1] - '0';
            items.push_back (it);
        }
        for (const auto& o : layout::oths0)
        {
            if (String (o.text) == "Mute" || String (o.text) == "Solo") continue;   // drawn under the graph, follow the crossovers
            Item it; it.kind = Btn; it.r = o.r; it.tip = U (o.tip); it.text = U (o.text); it.cls = o.cls; it.band = o.band;
            it.action = String (o.id).isNotEmpty() ? String (o.id) : U (o.text);
            items.push_back (it);
        }
        for (const auto& c : layout::cvs0)
        {
            Item it; it.kind = Cv; it.r = c.r; it.tip = U (c.tip); it.cv = c.id; it.band = c.band; it.cls = c.cls; items.push_back (it);
        }
        // a few bubbles that sit on dynamic text
        auto bub = [&] (layout::Rc r, const char* tip, int band, const char* cls)
        { Item it; it.kind = Btn; it.r = r; it.tip = tip; it.band = band; it.cls = cls; it.action = "tip"; items.push_back (it); };
        bub ({ 1053.0f, 285.0f, 172.0f, 17.0f }, "The limiter is always the last stage, whatever Mix, Boost, Max dB or Output gain you set: nothing leaves the plugin above the Ceiling. Change the Ceiling with the Ceil knob (-12 to 0 dB), or switch the limiter off with the Lim lamp.", -1, "tipOnly");
        for (int b = 0; b < 4; ++b)
            bub ({ BX[b] + 88.0f, 314.0f, 190.0f, 22.0f }, "Band and the range of sound it works on. Move the crossovers in the graph to change it.", b, "tipOnly");
    }

    // ---------------------------------------------------------------- drawing
    void drawBtn (Graphics& g, Rectangle<float> r, const String& text, bool on, float fs, bool tgb)
    {
        auto& t = T();
        g.setColour (on ? theme::sel : t.plate);
        g.fillRoundedRectangle (r, tgb ? 3.0f : 4.0f);
        g.setColour (on ? theme::selb : t.line);
        g.drawRoundedRectangle (r.reduced (0.5f), tgb ? 3.0f : 4.0f, 1.0f);
        g.setColour (on ? theme::lilac : t.ink);
        g.setFont (theme::font (FontKind::regular, fs));
        g.drawText (text, r.expanded (20.0f, 0.0f), Justification::centred, false);
    }
    void drawKnobShape (Graphics& g, Rectangle<float> r, float t01, Colour col, bool bipolar, float t0)
    {
        const float s = jmin (r.getWidth(), r.getHeight()), cx = r.getCentreX(), cy = r.getCentreY(), k = s / 100.0f;
        auto arc = [&] (float a0, float a1, Colour c, float w)
        {
            Path p; p.addCentredArc (cx, cy, 43 * k, 43 * k, 0.0f, degreesToRadians (a0), degreesToRadians (a1), true);
            g.setColour (c); g.strokePath (p, PathStrokeType (w * k, PathStrokeType::curved, PathStrokeType::rounded));
        };
        arc (-135.0f, 135.0f, Colour (0xff2a2c33), 7.0f);
        const float a = -135.0f + 270.0f * t01, a0 = -135.0f + 270.0f * (bipolar ? t0 : 0.0f);
        if (std::abs (a - a0) > 0.5f) arc (jmin (a, a0), jmax (a, a0), col, 7.0f);
        ColourGradient grad (Colour (0xfff1f1f4), cx - 10 * k, cy - 15 * k, Colour (0xff6e6f78), cx + 35 * k, cy + 35 * k, true);
        grad.addColour (0.55, Colour (0xffb9bac2));
        g.setGradientFill (grad); g.fillEllipse (cx - 31 * k, cy - 31 * k, 62 * k, 62 * k);
        g.setColour (Colour (0xff4a4b53)); g.drawEllipse (cx - 31 * k, cy - 31 * k, 62 * k, 62 * k, 2 * k);
        const float rad = degreesToRadians (a);
        g.setColour (Colour (0xff16171a));
        g.drawLine (cx, cy, cx + std::sin (rad) * 24 * k, cy - std::cos (rad) * 24 * k, 7 * k);
    }

    void paintItem (Graphics& g, const Item& it)
    {
        auto& t = T();
        auto r = R (it.r);
        switch (it.kind)
        {
            case Knob:
            {
                const float v = get (it.pid);
                const float pos = (float) posOf (it.pid, v);
                const auto* d = defOf (it.pid);
                const bool bip = d && d->mn < 0 && d->mx > 0 && ! d->log;
                drawKnobShape (g, R (it.knobR), pos, it.col, bip, d ? (float) posOf (it.pid, 0.0f) : 0.0f);
                const bool hor = it.cls.contains (" h"), hero = it.cls.contains ("hero");
                auto vr = R (it.valR), lr = R (it.labR);
                g.setColour (hero ? Colours::white : t.ink); g.setFont (theme::font (hero ? FontKind::semi : FontKind::mono, hero ? 13.0f : 10.5f));
                const auto vs = fmt (it.pid, v);
                if (hor) g.drawText (vs, Rectangle<float> (vr.getX(), vr.getY() - 2, vr.getWidth() + 30, vr.getHeight() + 4), Justification::centredLeft, false);
                else g.drawText (vs, vr.expanded (24.0f, 2.0f), Justification::centred, false);
                g.setColour (hero ? Colours::white : t.title);
                g.setFont (theme::font (FontKind::semi, hero ? 15.0f : 12.0f).withExtraKerningFactor ((hero ? 1.4f : 0.8f) / (hero ? 15.0f : 12.0f)));
                if (hor) g.drawText (it.label.toUpperCase(), Rectangle<float> (lr.getX(), lr.getY() - 2, lr.getWidth() + 30, lr.getHeight() + 4), Justification::centredLeft, false);
                else g.drawText (it.label.toUpperCase(), lr.expanded (24.0f, 2.0f), Justification::centred, false);
                break;
            }
            case Toggle: drawBtn (g, r, it.text, get (it.pid) > 0.5f, 12.0f, true); break;
            case Power:
            {
                const bool on = get (it.pid) > 0.5f;
                const Colour c = it.cls.contains (" g") ? theme::green : theme::red;
                auto cr = r.withSizeKeepingCentre (16.0f, 16.0f);
                if (on) { g.setColour (c.withAlpha (0.35f)); g.fillEllipse (cr.expanded (4.0f)); }
                g.setColour (on ? c : Colour (0xff3a0a1c)); g.fillEllipse (cr);
                g.setColour (Colours::black); g.drawEllipse (cr, 1.0f);
                if (on) { g.setColour (Colours::white.withAlpha (0.5f)); g.fillEllipse (cr.reduced (5.0f).translated (-1.0f, -1.0f)); }
                break;
            }
            case Seg:
            {
                const bool on = (int) std::round (get (it.pid)) == it.idx;
                g.setColour (on ? theme::sel : t.plate); g.fillRect (r);
                g.setColour (on ? theme::selb : t.line); g.drawRect (r, 1.0f);
                g.setColour (on ? theme::lilac : t.ink); g.setFont (theme::font (FontKind::regular, 13.0f));
                g.drawText (it.text, r.expanded (10.0f, 0.0f), Justification::centred, false);
                break;
            }
            case Select:
            {
                g.setColour (t.disp); g.fillRoundedRectangle (r, 4.0f);
                g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
                const int i = (int) std::round (get (it.pid)) - it.off;
                g.setColour (t.ink); g.setFont (theme::font (FontKind::regular, 14.0f));
                g.drawText (it.opts[jlimit (0, it.opts.size() - 1, i)], r.reduced (6.0f, 0.0f).withTrimmedRight (14.0f), Justification::centredLeft, false);
                Path c; c.addTriangle (r.getRight() - 14, r.getCentreY() - 2, r.getRight() - 6, r.getCentreY() - 2, r.getRight() - 10, r.getCentreY() + 3);
                g.setColour (t.dim); g.fillPath (c);
                break;
            }
            case Btn: paintBtn (g, it, r); break;
            case Cv: paintCanvas (g, it, r); break;
            default: break;
        }
    }

    bool btnOn (const Item& it) const
    {
        const String a = it.action;
        if (a == "bBypass") return get ("m.byp") > 0.5f;
        if (a == "Scope") return (int) get ("b" + String (it.band) + ".view") == 0;
        if (a == "Gain") return (int) get ("b" + String (it.band) + ".view") == 1;
        return false;
    }
    void paintBtn (Graphics& g, const Item& it, Rectangle<float> r)
    {
        auto& t = T(); const String a = it.action;
        if (a == "tip" || it.cls == "cap") return;
        if (it.text.contains (U ("\xe2\x97\x80")) || it.text.contains (U ("\xe2\x96\xb6")))
        {
            const bool left = it.text.contains (U ("\xe2\x97\x80"));
            g.setColour (t.plate); g.fillRoundedRectangle (r, 4.0f); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            Path tri; const float cx = r.getCentreX(), cy = r.getCentreY();
            if (left) tri.addTriangle (cx + 4, cy - 5, cx + 4, cy + 5, cx - 4, cy); else tri.addTriangle (cx - 4, cy - 5, cx - 4, cy + 5, cx + 4, cy);
            g.setColour (t.ink); g.fillPath (tri); return;
        }
        const bool tg = it.cls.contains ("tgb");
        String txt = it.text;
        if (a == "pName") txt = presetName.isEmpty() ? String ("Start Here") : presetName;
        if (a == "pName") { g.setColour (t.plate); g.fillRoundedRectangle (r, 4.0f); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f); g.setColour (t.ink); g.setFont (theme::font (FontKind::semi, 15.0f)); g.drawText (txt, r.reduced (6.0f, 0.0f), Justification::centred, false); return; }
        drawBtn (g, r, txt, btnOn (it), tg ? 12.0f : 14.0f, tg);
    }

    // ----- level meters (Pre and Post)
    void drawVU (Graphics& g, Rectangle<float> r, bool pre)
    {
        auto& t = T();
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        const float h = 96, mn = -60, mx = 6, ox = r.getX() + 1, oy = r.getY() + 1;
        auto Y = [&] (float d) { return oy + h - 8 - (clampf (d, mn, mx) - mn) / (mx - mn) * (h - 14); };
        ColourGradient grad (Colour (0xffFF1383), 0, Y (mx), theme::blue, 0, Y (mn), false);
        const double f12 = (mx - (-12)) / (double) (mx - mn), f0 = (mx - 0) / (double) (mx - mn);
        grad.clearColours();
        grad.addColour (0.0, Colour (0xffFF1383)); grad.addColour (jmin (1.0, f0), Colour (0xffFF1383));
        grad.addColour (jmin (1.0, f0 + 0.001), Colour (0xffA43DFF)); grad.addColour (f12, Colour (0xffA43DFF));
        grad.addColour (jmin (1.0, f12 + 0.001), Colour (0xff6CC9F5)); grad.addColour (1.0, Colour (0xff6CC9F5));
        g.setFont (theme::font (FontKind::mono, 9.0f));
        for (int tk : { 6, 0, -6, -12, -24, -36, -48 })
        {
            const float y = Y ((float) tk);
            g.setColour (Colour (0xff01FCDC)); g.fillRect (ox + 20, y, 3.0f, 1.0f);
            g.drawText (String (tk), Rectangle<float> (ox - 14, y - 6, 32, 12), Justification::centredRight, false);
        }
        for (int k = 0; k < 2; ++k)
        {
            const float pk = (pre ? proc.engine.prePk[k] : proc.engine.postPk[k]).load(), ms = (pre ? proc.engine.preMs[k] : proc.engine.postMs[k]).load();
            const float x = ox + 28 + k * 30, pd = l2d (pk), rd = 10.0f * std::log10 (ms + 1.0e-12f) + 3.0f;
            g.setGradientFill (grad);
            g.fillRect (x, Y (rd), 22.0f, Y (mn) - Y (rd));
            g.setColour (Colours::white); g.fillRect (x, Y (pd), 22.0f, 2.0f);
            bool& clip = (pre ? clipPre : clipPost)[k];
            if (pd >= 0.0f) clip = true;
            g.setColour (clip ? Colour (0xffFF1383) : Colour (0xff2a2c31)); g.fillRect (x, oy, 22.0f, 4.0f);
        }
        g.setColour (Colour (0xffCD92FE));
        g.drawText ("L", Rectangle<float> (ox + 29, oy + h - 11, 20, 12), Justification::centred, false);
        g.drawText ("R", Rectangle<float> (ox + 59, oy + h - 11, 20, 12), Justification::centred, false);
    }
    void drawLimGr (Graphics& g, Rectangle<float> r)
    {
        auto& t = T();
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        const float w = 34, h = 96, gr = clampf (proc.engine.limGr.load(), 0.0f, 12.0f), ox = r.getX() + 1, oy = r.getY() + 1;
        auto Y = [&] (float d) { return oy + 6 + d / 12.0f * (h - 26); };
        g.setColour (theme::pink); g.fillRect (ox + 3, oy + 6, 9.0f, Y (gr) - (oy + 6));
        g.setColour (t.line); g.drawRect (ox + 3.5f, oy + 5.5f, 9.0f, h - 26, 1.0f);
        g.setFont (theme::font (FontKind::mono, 9.0f)); g.setColour (theme::pink);
        for (int d : { 0, 3, 6, 9, 12 })
        {
            g.fillRect (ox + 13, Y ((float) d), 3.0f, 1.0f);
            g.drawText (d == 0 ? String ("0") : "-" + String (d), Rectangle<float> (ox + 17, Y ((float) d) - 6, 20, 12), Justification::centredLeft, false);
        }
        g.setColour (t.yellow); g.drawText (String (-gr, 1).replace ("-0.0", "0.0"), Rectangle<float> (ox, oy + h - 14, w, 12), Justification::centred, false);
        g.setColour (Colour (0xffCD92FE)); g.drawText ("GR dB", Rectangle<float> (ox - 4, oy + h - 5, w + 8, 12), Justification::centred, false);
    }

    // ----- frequency graph
    std::array<float, 3> xs() const { return { get ("m.x1"), get ("m.x2"), get ("m.x3") }; }
    std::vector<float> edges() const
    {
        std::vector<float> e { 20.0f }; const int n = bands(); const auto x = xs();
        for (int k = 0; k < n - 1; ++k) e.push_back (x[(size_t) k]);
        e.push_back (20000.0f); return e;
    }
    static float fxp (float f, float x0, float w) { return x0 + std::log (f / 20.0f) / std::log (1000.0f) * w; }
    static float xfp (float x, float x0, float w) { return 20.0f * std::pow (1000.0f, (x - x0) / w); }
    static constexpr float GW = 882, GH = 176, GX0 = 34, GY0 = 6, GX1 = GW - 30, GY1 = GH - 16;
    static float YDB (float d) { return GY0 + (12.0f - clampf (d, -12, 12)) / 24.0f * (GY1 - GY0); }
    static float SPY (float d) { return GY1 - (clampf (d, -96, -6) + 96.0f) / 90.0f * (GY1 - GY0); }

    void updateSpectrum()
    {
        const int w = proc.engine.ringW.load (std::memory_order_acquire);
        auto doOne = [&] (const float* ring, std::array<float, 2048>& db)
        {
            for (int i = 0; i < 4096; ++i) work[(size_t) i] = ring[(w + i) & (cs::Engine::kRing - 1)] * win[(size_t) i];
            std::fill (work.begin() + 4096, work.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (work.data());
            for (int k = 0; k < 2048; ++k)
            {
                const float m = work[(size_t) k] / (4096.0f * 0.25f), d = 20.0f * std::log10 (m + 1.0e-9f);
                db[(size_t) k] = d > db[(size_t) k] ? d : db[(size_t) k] * 0.82f + d * 0.18f;
            }
        };
        doOne (proc.engine.ringPre, dbPre); doOne (proc.engine.ringPost, dbPost);
    }
    void specPath (Path& p, const std::array<float, 2048>& db)
    {
        const double fs = proc.sampleRateNow; const int n = 2048; bool first = true; const float w = GX1 - GX0;
        for (int x = 0; x <= (int) w; ++x)
        {
            const float f0 = xfp (GX0 + (float) x, GX0, w), f1 = xfp (GX0 + (float) x + 1, GX0, w);
            const float b0 = f0 * 4096.0f / (float) fs, b1 = f1 * 4096.0f / (float) fs; float d;
            if (b1 - b0 < 1.0f)
            {
                const float bf = clampf ((b0 + b1) * 0.5f, 1.0f, (float) n - 2); const int i = (int) bf; const float fr = bf - (float) i;
                d = db[(size_t) i] * (1 - fr) + db[(size_t) i + 1] * fr;
            }
            else
            {
                float s = 0; int c = 0;
                for (int k = jmax (1, (int) b0); k <= jmin (n - 1, (int) std::ceil (b1)); ++k) { s += std::pow (10.0f, db[(size_t) k] / 10.0f); ++c; }
                d = 10.0f * std::log10 (s / (float) jmax (1, c) + 1.0e-12f);
            }
            const float y = SPY (d);
            if (first) { p.startNewSubPath (GX0 + (float) x, y); first = false; } else p.lineTo (GX0 + (float) x, y);
        }
    }
    bool soloAny() const { for (int q = 0; q < bands(); ++q) if (get ("b" + String (q) + ".solo") > 0.5f) return true; return false; }
    void drawGraph (Graphics& g, Rectangle<float> r)
    {
        auto& t = T();
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f);
        g.saveState(); g.reduceClipRegion (r.toNearestInt()); g.setOrigin (r.getPosition().toInt() + Point<int> (1, 1));
        const float pw = GX1 - GX0; const int n = bands(); auto ed2 = edges();
        g.setFont (theme::font (FontKind::mono, 9.0f));
        for (int d : { 12, 6, 0, -6, -12 })
        {
            const float y = YDB ((float) d); g.setColour (d == 0 ? Colour (0xff4a4d55) : t.line); g.drawHorizontalLine ((int) y, GX0, GX1);
            g.setColour (Colour (0xffCD92FE)); g.drawText ((d > 0 ? "+" : "") + String (d), Rectangle<float> (GX0 - 34, y - 6, 30, 12), Justification::centredRight, false);
        }
        {
            g.saveState(); g.addTransform (AffineTransform::rotation (-MathConstants<float>::halfPi).translated (7.0f, (GY0 + GY1) * 0.5f));
            g.setColour (Colour (0xffCD92FE)); g.drawText ("gain dB", Rectangle<float> (-40, -6, 80, 12), Justification::centred, false); g.restoreState();
        }
        static const float FL[] = { 25, 50, 125, 250, 500, 1000, 2000, 4000, 8000, 12000, 18000 };
        static const char* FT[] = { "25 Hz", "50 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "12 kHz", "18 kHz" };
        for (int k = 0; k < 11; ++k)
        {
            const float x = fxp (FL[k], GX0, pw); g.setColour (t.line); g.drawVerticalLine ((int) x, GY0, GY1);
            g.setColour (Colour (0xff6CC9F5)); g.drawText (FT[k], Rectangle<float> (clampf (x, GX0 + 14, GX1 - 10) - 25, GH - 12, 50, 12), Justification::centred, false);
        }
        for (int d : { -80, -60, -40, -20 })
        {
            const float y = SPY ((float) d); g.setColour (theme::green.withAlpha (0.85f));
            g.drawText (String (d), Rectangle<float> (GX1 + 4, y - 6, 30, 12), Justification::centredLeft, false); g.fillRect (GX1, y, 3.0f, 1.0f);
        }
        g.setColour (theme::green); g.drawText ("dBFS", Rectangle<float> (GX1 + 3, GY0 - 2, 34, 10), Justification::centredLeft, false);
        const bool anySolo = soloAny();
        for (int b = 0; b < n; ++b)
        {
            const String bs = "b" + String (b) + ".";
            const float x0 = fxp (ed2[(size_t) b], GX0, pw), x1 = fxp (ed2[(size_t) b + 1], GX0, pw), cur = proc.engine.band[b].cur, gy = YDB (cur), y0 = YDB (0);
            g.setColour (BC[b].withAlpha (0.13f)); g.fillRect (x0, GY0, x1 - x0, GY1 - GY0);
            g.setColour (BC[b].withAlpha (0.28f)); g.fillRect (x0 + 1, jmin (y0, gy), x1 - x0 - 2, jmax (1.0f, std::abs (gy - y0)));
            g.setColour (BC[b]); g.drawLine (x0 + 2, gy, x1 - 2, gy, 1.8f);
            const bool muted = get (bs + "mute") > 0.5f || (anySolo && get (bs + "solo") < 0.5f), off = get (bs + "on") < 0.5f;
            if (muted || off) { g.setColour (Colours::black.withAlpha (0.5f)); g.fillRect (x0, GY0, x1 - x0, GY1 - GY0); }
            if (x1 - x0 > 46)
            {
                g.setColour (BC[b]); g.setFont (theme::font (FontKind::bold, 10.0f));
                g.drawText (fDb (cur), Rectangle<float> (x0 + 5, clampf (gy < GY0 + 22 ? gy + 12 : gy - 4, GY0 + 10, GY1 - 4) - 10, 80, 12), Justification::centredLeft, false);
                g.setFont (theme::font (FontKind::mono, 9.0f));
            }
        }
        const int fm = (int) get ("m.fft");
        if (fm > 0)
        {
            g.saveState(); g.reduceClipRegion (Rectangle<int> ((int) GX0, (int) GY0, (int) pw, (int) (GY1 - GY0)));
            if (fm == 1 || fm == 2) { Path p; specPath (p, dbPre); g.setColour (Colour (0xd9c8ccd6)); g.strokePath (p, PathStrokeType (1.2f)); }
            if (fm == 1 || fm == 3) { Path p; specPath (p, dbPost); g.setColour (theme::green); g.strokePath (p, PathStrokeType (1.4f)); }
            g.restoreState();
        }
        for (int k = 1; k < n; ++k)
        {
            const float x = fxp (ed2[(size_t) k], GX0, pw); g.setColour (Colour (0xffe8e9ee)); g.drawLine (x, GY0, x, GY1, 1.5f);
            Path tri; tri.addTriangle (x - 5, GY0, x + 5, GY0, x, GY0 + 7); g.fillPath (tri);
            const bool right = x > GX1 - 60; g.setColour (t.yellow); g.setFont (theme::font (FontKind::bold, 11.0f));
            g.drawText (fHz (ed2[(size_t) k]), Rectangle<float> (right ? x - 86 : x + 6, GY0 + 5, 80, 14), right ? Justification::centredRight : Justification::centredLeft, false);
        }
        g.setColour (t.line); g.drawRect (GX0, GY0, pw, GY1 - GY0, 1.0f);
        g.restoreState();
        g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }

    // ----- band displays
    static constexpr float SW = 271, SCH = 78;
    void legend (Graphics& g, float w)
    {
        g.setFont (theme::font (FontKind::bold, 10.0f)); float x = w - 6;
        const std::pair<const char*, unsigned> lg[] = { { "both", 0xffb9bcc6 }, { "shaped", 0xff02EBBA }, { "original", 0xffFF3D5A } };
        for (auto& l : lg)
        {
            GlyphArrangement ga; ga.addLineOfText (g.getCurrentFont(), l.first, 0.0f, 0.0f); const float tw = ga.getBoundingBox (0, -1, true).getWidth();
            g.setColour (argb (l.second)); g.drawText (l.first, Rectangle<float> (x - tw - 4, 1, tw + 8, 12), Justification::centred, false);
            x -= tw + 8;
        }
    }
    void drawScope (Graphics& g, int b)
    {
        auto& t = T(); const float w = SW, h = SCH, cy = h * 0.5f, A = (h - 22) * 0.5f;
        const int N = roundToInt (480.0f / (1.0f + get ("b" + String (b) + ".zoom") * 7.0f));
        auto ampY = [&] (float a) { return clampf ((l2d (a) + 54.0f) / 54.0f, 0.0f, 1.0f) * A; };
        g.setFont (theme::font (FontKind::mono, 9.0f));
        for (int d : { 0, -12, -24, -48 })
        {
            const float y = cy - (d + 54.0f) / 54.0f * A;
            g.setColour (t.line); g.drawHorizontalLine ((int) y, 24, w - 3); g.drawHorizontalLine ((int) (2 * cy - y), 24, w - 3);
            g.setColour (Colour (0xff01FCDC)); g.drawText (String (d), Rectangle<float> (2, y - 6, 24, 12), Justification::centredLeft, false);
        }
        auto X = [&] (int k) { return 26.0f + (float) k / (float) (N - 1) * (w - 30.0f); };
        const int pos = proc.engine.scPos[b].load (std::memory_order_acquire);
        const float* I = proc.engine.scI[b]; const float* O = proc.engine.scO[b];
        auto at = [&] (const float* a, int k) { return a[(pos + cs::Engine::kHist - N + k) % cs::Engine::kHist]; };
        auto path = [&] (auto fn, Colour col, float al)
        {
            Path p; for (int k = 0; k < N; ++k) { const float x = X (k), y = cy - ampY (fn (k)); if (k) p.lineTo (x, y); else p.startNewSubPath (x, y); }
            for (int k = N - 1; k >= 0; --k) p.lineTo (X (k), cy + ampY (fn (k)));
            p.closeSubPath(); g.setColour (col.withAlpha (al)); g.fillPath (p);
        };
        path ([&] (int k) { return at (I, k); }, Colour (0xffFF3D5A), 0.85f);
        path ([&] (int k) { return at (O, k); }, Colour (0xff02EBBA), 0.85f);
        path ([&] (int k) { return jmin (at (I, k), at (O, k)); }, Colour (0xffb9bcc6), 0.9f);
        const float thr = get ("b" + String (b) + ".thr"), ty = cy - (clampf (thr, -54, 0) + 54.0f) / 54.0f * A;
        g.setColour (Colours::white);
        for (float yy : { ty, 2 * cy - ty }) for (float x = 24; x < w - 3; x += 6) g.fillRect (x, yy, 3.0f, 1.0f);
        g.setColour (Colour (0xff6CC9F5)); g.setFont (theme::font (FontKind::mono, 9.0f));
        const float secs = (float) N * 0.006f; const float fr[3] = { 1.0f, 0.5f, 0.0f };
        for (float f : fr) g.drawText (f == 0 ? String ("now") : "-" + String (secs * f, 2) + "s", Rectangle<float> (clampf (26 + (1 - f) * (w - 30), 24, w - 16) - 30, h - 13, 60, 12), Justification::centred, false);
        legend (g, w);
    }
    void drawGainHist (Graphics& g, int b)
    {
        auto& t = T(); const float w = SW, h = SCH;
        const int N = roundToInt (360.0f / (1.0f + get ("b" + String (b) + ".zoom") * 7.0f));
        auto Y = [&] (float d) { return 7 + (12 - clampf (d, -12, 12)) / 24.0f * (h - 26); };
        g.setFont (theme::font (FontKind::mono, 9.0f));
        for (int d : { 12, 6, 0, -6, -12 })
        {
            g.setColour (d == 0 ? Colour (0xff4a4d55) : t.line); g.drawHorizontalLine ((int) Y ((float) d), 26, w - 3);
            g.setColour (Colour (0xffCD92FE)); g.drawText ((d > 0 ? "+" : "") + String (d), Rectangle<float> (2, Y ((float) d) - 6, 24, 12), Justification::centredLeft, false);
        }
        const int pos = proc.engine.ghPos.load (std::memory_order_acquire); const float* h0 = proc.engine.grHist[b];
        Path p; p.startNewSubPath (26, Y (0));
        for (int k = 0; k < N; ++k) p.lineTo (26.0f + (float) k / (float) (N - 1) * (w - 30), Y (h0[(pos + cs::Engine::kGh - N + k) % cs::Engine::kGh]));
        Path fillP (p); fillP.lineTo (w - 4, Y (0)); fillP.closeSubPath();
        g.setColour (BC[b].withAlpha (0.35f)); g.fillPath (fillP);
        Path line; for (int k = 0; k < N; ++k) { const float x = 26.0f + (float) k / (float) (N - 1) * (w - 30), y = Y (h0[(pos + cs::Engine::kGh - N + k) % cs::Engine::kGh]); if (k) line.lineTo (x, y); else line.startNewSubPath (x, y); }
        g.setColour (BC[b]); g.strokePath (line, PathStrokeType (1.6f));
        g.setColour (Colour (0xff6CC9F5));
        const float secs = (float) N / 60.0f; const float fr[3] = { 1.0f, 0.5f, 0.0f };
        for (float f : fr) g.drawText (f == 0 ? String ("now") : "-" + String (secs * f, 1) + "s", Rectangle<float> (clampf (26 + (1 - f) * (w - 30), 26, w - 14) - 30, h - 13, 60, 12), Justification::centred, false);
    }
    void drawLevel (Graphics& g, int b)
    {
        auto& t = T(); const String bs = "b" + String (b) + ".";
        const float w = SW, h = 36, lvl = proc.engine.band[b].lvl, gate = proc.engine.band[b].gate;
        auto X = [&] (float d) { return 6 + (clampf (d, -60, 0) + 60) / 60.0f * (w - 12); };
        g.setFont (theme::font (FontKind::mono, 9.0f)); g.setColour (Colour (0xff01FCDC));
        const std::pair<float, const char*> tk[] = { { -60, "-inf" }, { -48, "-48" }, { -36, "-36" }, { -24, "-24" }, { -12, "-12" }, { 0, "0" } };
        for (auto& q : tk) { g.drawText (q.second, Rectangle<float> (clampf (X (q.first), 12, w - 8) - 16, 14, 32, 12), Justification::centred, false); g.fillRect (X (q.first), 12.0f, 1.0f, 3.0f); }
        const float ew = X (lvl) - X (-60);
        if (ew > 0)
        {
            ColourGradient gr2 (Colour (0xff6CC9F5), X (-60), 0, Colour (0xffFF1383), X (0), 0, false); gr2.clearColours();
            gr2.addColour (0.0, Colour (0xff6CC9F5)); gr2.addColour (0.8, Colour (0xff6CC9F5)); gr2.addColour (0.801, Colour (0xffA43DFF)); gr2.addColour (0.999, Colour (0xffA43DFF)); gr2.addColour (1.0, Colour (0xffFF1383));
            g.setGradientFill (gr2); g.fillRect (X (-60), 3.0f, ew, 8.0f);
        }
        g.setColour (t.line); g.drawRect (X (-60) - 0.5f, 2.5f, X (0) - X (-60) + 1, 9.0f, 1.0f);
        const float thr = get (bs + "thr"), tx = X (thr);
        g.setColour (Colours::white); g.fillRect (tx - 2, 0.0f, 4.0f, 14.0f);
        g.setColour (t.yellow); g.setFont (theme::font (FontKind::bold, 10.0f));
        g.drawText ("Threshold " + String (thr, 1) + " dB", Rectangle<float> (8, 24, 150, 12), Justification::centredLeft, false);
        const bool on = get (bs + "on") > 0.5f && gate > 0.5f && (std::abs (get (bs + "atk")) + std::abs (get (bs + "sus")) > 0);
        g.setColour (on ? theme::green : t.dim);
        g.drawText (get (bs + "on") < 0.5f ? "band off" : on ? "shaping" : "idle", Rectangle<float> (w - 108, 24, 100, 12), Justification::centredRight, false);
    }
    void drawGainMeter (Graphics& g, int b)
    {
        auto& t = T(); const float w = SW, h = 18, cur = proc.engine.band[b].cur, v = clampf (cur, -12, 12);
        auto X = [&] (float d) { return 6 + (clampf (d, -12, 12) + 12) / 24.0f * (w - 12); };
        g.setColour (v >= 0 ? t.yellow : Colour (0xffCD92FE));
        const float x0 = X (0), x1 = X (v); g.fillRect (jmin (x0, x1), 2.0f, std::abs (x1 - x0), 6.0f);
        g.setColour (t.line); g.drawRect (X (-12) - 0.5f, 1.5f, X (12) - X (-12) + 1, 7.0f, 1.0f);
        g.setColour (Colours::white); g.fillRect (x0 - 0.5f, 0.0f, 1.0f, 10.0f);
        g.setFont (theme::font (FontKind::mono, 9.0f)); g.setColour (Colour (0xffCD92FE));
        const std::pair<float, const char*> tk[] = { { -12, "-12" }, { -6, "-6" }, { 0, "0" }, { 6, "+6" }, { 12, "+12" } };
        for (auto& q : tk) g.drawText (q.second, Rectangle<float> (clampf (X (q.first), 10, w - 8) - 16, h - 11, 32, 12), Justification::centred, false);
        const String tx = "Gain " + fDb (cur); const auto rr = Rectangle<float> (8, -2, 120, 12);
        g.setFont (theme::font (FontKind::mono, 9.0f));
        g.setColour (t.disp); for (float dx : { -1.0f, 1.0f }) for (float dy : { -1.0f, 1.0f }) g.drawText (tx, rr.translated (dx, dy), Justification::centredLeft, false);
        g.setColour (Colours::white); g.drawText (tx, rr, Justification::centredLeft, false);
    }
    void paintCanvas (Graphics& g, const Item& it, Rectangle<float> r)
    {
        auto& t = T();
        if (it.cv == 1) { drawVU (g, r, true); return; }
        if (it.cv == 3) { drawVU (g, r, false); return; }
        if (it.cv == 4) { drawLimGr (g, r); return; }
        if (it.cv == 2) { drawGraph (g, r); return; }
        const int b = it.band; if (b < 0 || b > 3) return;
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        g.saveState(); g.reduceClipRegion (r.toNearestInt()); g.setOrigin (r.getPosition().toInt() + Point<int> (1, 1));
        const int sub = (it.cv - 5) % 3;
        if (sub == 0) { if ((int) get ("b" + String (b) + ".view") == 0) drawScope (g, b); else drawGainHist (g, b); }
        else if (sub == 1) drawLevel (g, b);
        else drawGainMeter (g, b);
        g.restoreState();
    }

    // ---------------------------------------------------------------- the whole window
    struct MS { int band; bool solo; Rectangle<float> r; };
    std::vector<MS> msRects;
    void drawMuteSolo (Graphics& g)
    {
        const int n = bands(); auto ed2 = edges(); const float gx = 172 + 1 + GX0, pw = GX1 - GX0; auto& t = T();
        msRects.clear();
        for (int i = 0; i < n; ++i)
        {
            const float x0 = gx + fxp (ed2[(size_t) i], 0, pw), x1 = gx + fxp (ed2[(size_t) i + 1], 0, pw);
            auto r = Rectangle<float> (x0, 274, x1 - x0, 22);
            g.setColour (BC[i].withAlpha (0.10f)); g.fillRect (r); g.setColour (BC[i].withAlpha (0.6f)); g.drawRect (r, 1.0f);
            const bool narrow = r.getWidth() < 112;
            for (int k = 0; k < 2; ++k)
            {
                const bool solo = k == 1; const String txt = narrow ? (solo ? "S" : "M") : (solo ? "Solo" : "Mute");
                const float w = narrow ? 30.0f : 52.0f, gap = narrow ? 2.0f : 8.0f;
                const float bx = r.getCentreX() - w - gap * 0.5f + (float) k * (w + gap);
                auto cell = Rectangle<float> (bx, r.getY() + 2, w, 18.0f);
                const bool on = get ("b" + String (i) + (solo ? ".solo" : ".mute")) > 0.5f;
                auto box = Rectangle<float> (cell.getX() + 3, cell.getCentreY() - 6.5f, 13.0f, 13.0f);
                g.setColour (on ? BC[i] : t.disp); g.fillRoundedRectangle (box, 2.0f);
                g.setColour (on ? BC[i] : t.dim); g.drawRoundedRectangle (box.reduced (0.5f), 2.0f, 1.0f);
                g.setColour (t.ink); g.setFont (theme::font (FontKind::regular, 12.0f));
                g.drawText (txt, Rectangle<float> (box.getRight() + 4, cell.getY(), cell.getRight() - box.getRight() - 4, cell.getHeight()), Justification::centredLeft, false);
                msRects.push_back ({ i, solo, cell });
            }
        }
    }
    void paintAll (Graphics& g)
    {
        auto& t = T();
        g.fillAll (t.bg);
        g.drawImage (woodL, Rectangle<float> (0, 0, 52, WH));
        g.drawImage (woodR, Rectangle<float> (1228, 0, 52, WH));
        for (const auto& b : layout::boxes0)
        {
            const String cls (b.cls);
            if (cls == "rig" || cls == "tag" || cls == "ms" || cls == "lamp") continue;
            auto r = R (b.r);
            if (cls == "main") r.setHeight (WH);
            if (cls == "foot") r.translate (0, -RIGH);
            if (cls == "hdr") g.setColour (t.plate2); else if (cls == "main") g.setColour (t.plate); else g.setColour (argb (b.bg));
            if (b.rad > 0) g.fillRoundedRectangle (r, b.rad); else g.fillRect (r);
            if (b.bd > 0) { g.setColour (cls == "hdr" || cls == "foot" ? t.line : argb (b.bc)); if (b.rad > 0) g.drawRoundedRectangle (r.reduced (0.5f), b.rad, b.bd); else if (cls == "foot") g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight()); else g.drawRect (r, b.bd); }
            if (cls == "hdr") { g.setColour (t.line); g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX(), r.getRight()); }
        }
        drawMuteSolo (g);
        const int nb = bands(); const auto ed2 = edges();
        for (const auto& s : layout::txts0)
        {
            const String cls (s.cls);
            if (cls == "ceiln" || cls == "SMALL") continue;
            FontKind fk = s.mono ? FontKind::mono : s.fw >= 800 ? FontKind::brand : s.fw >= 700 ? FontKind::bold : s.fw >= 600 ? FontKind::semi : FontKind::regular;
            auto f = theme::font (fk, s.fs); if (s.ls != 0) f = f.withExtraKerningFactor (s.ls / s.fs);
            g.setFont (f); g.setColour (argb (s.col)); String str = U (s.s); if (s.upper) str = str.toUpperCase();
            g.drawText (str, Rectangle<float> (s.r.x, s.r.y - 2, s.r.w + 30, s.r.h + 4), Justification::centredLeft, false);
        }
        // band range text
        for (const auto& s : layout::txts0)
        {
            if (String (s.cls) != "SMALL") continue;
            int b = 0; for (int k = 0; k < 4; ++k) if (s.r.x > BX[k]) b = k;
            const String str = b < nb ? fHz (ed2[(size_t) b]) + " to " + fHz (ed2[(size_t) b + 1]) : String ("unused");
            g.setFont (theme::font (FontKind::mono, s.fs).withExtraKerningFactor (s.ls / s.fs)); g.setColour (argb (s.col));
            g.drawText (str, Rectangle<float> (s.r.x, s.r.y - 2, s.r.w + 30, s.r.h + 4), Justification::centredLeft, false);
        }
        if (logo) logo->drawWithin (g, Rectangle<float> (62, 7, 32, 32), RectanglePlacement::centred, 1.0f);
        for (const auto& it : items) paintItem (g, it);
        // latency and ceiling note
        g.setColour (Colour (0xff6CC9F5)); g.setFont (theme::font (FontKind::regular, 10.0f).withExtraKerningFactor (0.6f / 10.0f));
        g.drawText ("Latency " + String ((double) proc.engine.latencySamples() * 1000.0 / jmax (1.0, proc.sampleRateNow), 1) + " ms", Rectangle<float> (60, 230, 106, 14), Justification::centred, false);
        {
            const bool lim = get ("m.limOn") > 0.5f;
            g.setColour (lim ? theme::green : Colour (0xffFF3D5A)); g.fillEllipse (1053.2f, 290.5f, 8.0f, 8.0f);
            g.setColour (lim ? Colour (0xff6fe3a6) : Colour (0xffFF5A7A)); g.setFont (theme::font (FontKind::mono, 9.5f));
            g.drawText (lim ? "Output never above " + String (get ("m.ceil"), 1) + " dBFS" : String ("Limiter off: output not capped"), Rectangle<float> (1065.2f, 286.0f, 190.0f, 17.0f), Justification::centredLeft, false);
        }
        // unused bands are greyed out
        for (int b = nb; b < 4; ++b) { g.setColour (Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (BX[b], 304, 285.5f, 344, 4.0f); }
        // footer
        g.setColour (t.dim); g.setFont (theme::font (FontKind::regular, 12.0f).withExtraKerningFactor (1.0f / 12.0f));
        g.drawText (U ("Studio 87 \xc2\xb7 ClawShaper v") + JucePlugin_VersionString, Rectangle<float> (64, 696 - RIGH, 500, 24), Justification::centredLeft, false);
        g.drawText (U ("1280 \xc3\x97 680 px"), Rectangle<float> (1028, 696 - RIGH, 200, 24), Justification::centredRight, false);
        if (toastTicks > 0) { auto r = Rectangle<float> (440, 628, 400, 26); g.setColour (Colours::black); g.fillRoundedRectangle (r, 5); g.setColour (theme::selb); g.drawRoundedRectangle (r.reduced (0.5f), 5, 1); g.setColour (t.ink); g.setFont (theme::font (FontKind::regular, 14.0f)); g.drawText (toastText, r, Justification::centred, false); }
        if (tips && tipText.isNotEmpty() && tipItem >= 0)
        {
            const int tw = 296;
            AttributedString as; as.setWordWrap (AttributedString::byWord); as.append (tipText, theme::font (FontKind::regular, 14.0f), t.ink);
            TextLayout tl; tl.createLayout (as, (float) tw - 20.0f);
            const float th = tl.getHeight() + 14.0f; float x = jlimit (56.0f, 1224.0f - tw, tipPos.x), y = tipPos.y + 22.0f;
            if (y + th > WH - 4) y = tipPos.y - th - 8;
            auto r = Rectangle<float> (x, y, (float) tw, th); g.setColour (Colours::black); g.fillRoundedRectangle (r, 5); g.setColour (theme::selb); g.drawRoundedRectangle (r.reduced (0.5f), 5, 1);
            tl.draw (g, r.reduced (10.0f, 7.0f));
        }
    }

    // ---------------------------------------------------------------- presets
    File userDir() const { return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Studio 87").getChildFile ("ClawShaper").getChildFile ("presets"); }
    void applyKV (const std::vector<std::pair<String, float>>& kv)
    {
        for (const auto& dd : cs::kParams)
        {
            const String id (dd.id); if (id == "m.byp" || id == "m.listen") continue;
            setUser (id, dd.df);
        }
        for (auto& p : kv) setUser (p.first, p.second);
    }
    void applyPreset (int idx)
    {
        const auto& pr = presets::kPre[idx]; std::vector<std::pair<String, float>> kv;
        for (int i = 0; i < pr.n; ++i) kv.push_back ({ pr.kv[i].id, pr.kv[i].v });
        applyKV (kv); setName (pr.name); toast (String ("Loaded ") + pr.name);
    }
    void setName (const String& n) { presetName = n; proc.setSetting (setting::preset, n); ed.repaint(); }
    StringArray userNames() const
    {
        StringArray s; for (auto& f : userDir().findChildFiles (File::findFiles, false, "*.csp")) s.add (f.getFileNameWithoutExtension()); s.sort (true); return s;
    }
    void applyUser (const String& name)
    {
        if (auto xml = XmlDocument::parse (userDir().getChildFile (name + ".csp")))
        {
            std::vector<std::pair<String, float>> kv;
            for (auto* e : xml->getChildIterator()) kv.push_back ({ e->getStringAttribute ("id"), (float) e->getDoubleAttribute ("v") });
            applyKV (kv); setName (name); toast ("Loaded " + name);
        }
    }
    void saveUser (const String& rawName)
    {
        const String name = rawName.removeCharacters ("\\/:*?\"<>|").trim().substring (0, 40); if (name.isEmpty()) return;
        auto f = userDir().getChildFile (name + ".csp"); f.getParentDirectory().createDirectory();
        XmlElement x ("ClawShaperPreset");
        for (const auto& dd : cs::kParams) { const String id (dd.id); if (id == "m.byp" || id == "m.listen") continue; auto* e = x.createNewChildElement ("p"); e->setAttribute ("id", id); e->setAttribute ("v", (double) get (id)); }
        if (x.writeTo (f)) { setName (name); toast ("Saved " + name); } else toast ("Could not save the preset");
    }
    void askSave()
    {
        auto* w = new AlertWindow ("Save preset", "Name for your preset. Saving with an existing name replaces it.", MessageBoxIconType::NoIcon, &ed);
        w->addTextEditor ("n", presetName, "Name"); w->addButton ("Save", 1); w->addButton ("Cancel", 0);
        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
        {
            const String n = w->getTextEditorContents ("n");
            if (r == 1) { if (userDir().getChildFile (n.removeCharacters ("\\/:*?\"<>|").trim() + ".csp").existsAsFile()) askOverwrite (n); else saveUser (n); }
        }), true);
    }
    void askOverwrite (const String& n)
    {
        auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon).withTitle ("Replace preset?").withMessage ("A preset called \"" + n + "\" exists. Replace it?").withButton ("Replace").withButton ("Cancel");
        AlertWindow::showAsync (opts, [this, n] (int r) { if (r == 1) saveUser (n); });
    }
    void stepPreset (int dir)
    {
        int cur = -1; for (int i = 0; i < presets::kNumPre; ++i) if (presetName == presets::kPre[i].name) cur = i;
        cur = cur < 0 ? (dir > 0 ? 0 : presets::kNumPre - 1) : (cur + dir + presets::kNumPre) % presets::kNumPre;
        applyPreset (cur);
    }
    void presetMenu (Point<int> where)
    {
        PopupMenu m; StringArray types;
        for (int i = 0; i < presets::kNumPre; ++i) types.addIfNotAlreadyThere (presets::kPre[i].type);
        for (const auto& ty : types)
        {
            PopupMenu sub;
            for (int i = 0; i < presets::kNumPre; ++i) if (String (presets::kPre[i].type) == ty) sub.addItem (i + 1, presets::kPre[i].name, true, presetName == presets::kPre[i].name);
            m.addSubMenu (ty, sub);
        }
        const auto un = userNames();
        if (un.size() > 0) { PopupMenu sub; for (int i = 0; i < un.size(); ++i) sub.addItem (1000 + i, un[i], true, presetName == un[i]); m.addSubMenu ("My presets", sub); }
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (where.x, where.y, 1, 1)), [this, un] (int r)
        { if (r >= 1000) applyUser (un[r - 1000]); else if (r > 0) applyPreset (r - 1); });
    }

    // ---------------------------------------------------------------- actions
    void toast (const String& s) { toastText = s; toastTicks = 70; ed.repaint(); }
    void doAction (const Item& it, Point<int> screenPt)
    {
        const String a = it.action; const String bs = "b" + String (it.band) + ".";
        if (a == "bBypass") setUser ("m.byp", get ("m.byp") > 0.5f ? 0.0f : 1.0f);
        else if (a == "Scope") setUser (bs + "view", 0.0f);
        else if (a == "Gain") setUser (bs + "view", 1.0f);
        else if (a == "pPrev") stepPreset (-1);
        else if (a == "pNext") stepPreset (1);
        else if (a == "pName") presetMenu (screenPt);
        else if (a == "pSave") askSave();
        else if (a == "bSettings") settingsMenu (screenPt);
        else if (a == "bGuide" || a == "bAbout") aboutBox (a);
    }
    void settingsMenu (Point<int> where)
    {
        PopupMenu m; m.addSectionHeader ("Settings");
        m.addItem (1, "Info bubbles", true, tips);
        m.addItem (2, "Power saving (slower redraw)", true, (bool) proc.getSetting (setting::power, false));
        PopupMenu lk; lk.addItem (11, "A  neon navy", true, look == 'A'); lk.addItem (12, "B  purple soda pop", true, look == 'B'); lk.addItem (13, "C  charcoal", true, look == 'C');
        m.addSubMenu ("Colour look", lk);
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (where.x, where.y, 1, 1)), [this] (int r)
        {
            if (r == 1) { tips = ! tips; proc.setSetting (setting::tips, tips); }
            else if (r == 2) { const bool v = ! (bool) proc.getSetting (setting::power, false); proc.setSetting (setting::power, v); ed.setRate (v ? 12 : 30); }
            else if (r >= 11 && r <= 13) { look = (char) ('A' + (r - 11)); proc.setSetting (setting::look, String::charToString ((juce::juce_wchar) look)); }
            ed.repaint();
        });
    }
    void aboutBox (const String& which)
    {
        String msg;
        if (which == "bGuide")
            msg = "ClawShaper shapes the punch (attack) and the tail (sustain) of your sound in up to four bands. Attack: positive = more punch, negative = softer. Sustain: positive = longer tails, negative = tighter. Threshold: below it nothing is shaped. Listen: hear only the attack or the sustain part. Boost multiplies the effect. The limiter is always last and holds the Ceiling. Mix 0% is the untouched, delayed input. Hover any control for an info bubble. Double-click a knob to reset it; Shift while dragging for fine steps. Keys: B bypass, [ and ] previous and next preset.";
        else
            msg = "ClawShaper by Studio 87. Free and open source (AGPL-3.0-or-later). No telemetry, no data collection, no network access. Your own presets are stored in your user data folder under Studio 87/ClawShaper/presets. https://github.com/SpeakingColors/clawshaper";
        auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::NoIcon).withTitle ("ClawShaper").withMessage (msg).withButton ("OK");
        AlertWindow::showAsync (opts, nullptr);
    }

    // ---------------------------------------------------------------- mouse
    int hitTest (Point<float> p) const
    {
        for (int i = (int) items.size() - 1; i >= 0; --i)
        {
            const auto& it = items[(size_t) i]; if (! bandActive (it.band)) continue;
            if (it.tip.isEmpty()) continue;
            if (R (it.r).contains (p)) return i;
        }
        return -1;
    }
    bool interactive (const Item& it) const { return it.kind != Btn || (it.action != "tip" && it.cls != "cap"); }
    Point<float> toLayout (Point<float> p) const { const float s = (float) ed.getWidth() / 1280.0f; return { p.x / s, p.y / s }; }
    void down (const MouseEvent& e)
    {
        const auto p = toLayout (e.position); dragItem = -1; dragKind = {};
        if (p.x < 52 || p.x > 1228) return;
        for (const auto& m : msRects) if (m.r.contains (p)) { const String id = "b" + String (m.band) + (m.solo ? ".solo" : ".mute"); setUser (id, get (id) > 0.5f ? 0.0f : 1.0f); return; }
        const int i = hitTest (p);
        if (i < 0) return;
        const auto& it = items[(size_t) i]; if (! interactive (it)) return;
        dragItem = i; dragStart = e.position;
        switch (it.kind)
        {
            case Knob: dragStartNorm = posOf (it.pid, get (it.pid)); begin (it.pid); break;
            case Toggle: case Power: setUser (it.pid, get (it.pid) > 0.5f ? 0.0f : 1.0f); break;
            case Radio: case Seg: setUser (it.pid, (float) it.idx); break;
            case Select:
            {
                PopupMenu m; const int cur = (int) std::round (get (it.pid)) - it.off;
                for (int k = 0; k < it.opts.size(); ++k) m.addItem (k + 1, it.opts[k], true, k == cur);
                const String pid = it.pid; const int off = it.off; const float s = (float) ed.getWidth() / 1280.0f;
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (&ed).withTargetScreenArea (ed.localAreaToGlobal (Rectangle<float> (R (it.r).getX() * s, R (it.r).getBottom() * s, 1, 1).toNearestInt())),
                                 [this, pid, off] (int r) { if (r > 0) setUser (pid, (float) (r - 1 + off)); });
                break;
            }
            case Btn: doAction (it, ed.localPointToGlobal (e.getPosition())); break;
            case Cv: canvasDown (it, p); break;
        }
    }
    void canvasDown (const Item& it, Point<float> p)
    {
        if (it.cv == 1) { clipPre[0] = clipPre[1] = false; return; }
        if (it.cv == 3) { clipPost[0] = clipPost[1] = false; return; }
        if (it.cv == 2)
        {
            const float mx = p.x - it.r.x - 1, my = p.y - it.r.y - 1; const int n = bands(); auto ed2 = edges(); const float pw = GX1 - GX0;
            for (int k = 1; k < n; ++k) if (std::abs (mx - fxp (ed2[(size_t) k], GX0, pw)) < 6 && my > GY0 && my < GY1) { dragKind = "x"; dragBand = k - 1; begin ("m.x" + String (k)); return; }
            return;
        }
        if (it.band >= 0 && (it.cv - 5) % 3 == 1) { dragKind = "thr"; dragBand = it.band; begin ("b" + String (it.band) + ".thr"); thrSet (it, p); }
    }
    void thrSet (const Item& it, Point<float> p)
    {
        const float mx = p.x - it.r.x - 1; const float d = -60 + (mx - 6) / (SW - 12) * 60;
        set ("b" + String (it.band) + ".thr", std::round (clampf (d, -60, 0) * 2) / 2);
    }
    void drag (const MouseEvent& e)
    {
        if (dragItem < 0 || dragItem >= (int) items.size()) return;
        const auto& it = items[(size_t) dragItem]; const float s = (float) ed.getWidth() / 1280.0f;
        if (it.kind == Knob)
        {
            const double dy = -(e.position.y - dragStart.y) / s;
            set (it.pid, valOf (it.pid, dragStartNorm + dy / (e.mods.isShiftDown() ? 1500.0 : 170.0)));
        }
        else if (it.kind == Cv)
        {
            const auto p = toLayout (e.position);
            if (dragKind == "x")
            {
                const float mx = p.x - it.r.x - 1; float f = xfp (clampf (mx, GX0, GX1), GX0, GX1 - GX0);
                const float g = 1.18f; const auto x = xs(); const int k = dragBand;
                const float lo = k == 0 ? 20.0f : x[(size_t) k - 1] * g, hi = k == bands() - 2 ? 20000.0f / g : x[(size_t) k + 1] / g;
                set ("m.x" + String (k + 1), clampf (f, lo, jmax (lo, hi)));
            }
            else if (dragKind == "thr") thrSet (it, p);
        }
    }
    void up()
    {
        if (dragItem >= 0 && dragItem < (int) items.size())
        {
            const auto& it = items[(size_t) dragItem];
            if (it.kind == Knob) end (it.pid);
            if (dragKind == "x") end ("m.x" + String (dragBand + 1));
            else if (dragKind == "thr") end ("b" + String (dragBand) + ".thr");
        }
        dragItem = -1; dragKind = {};
    }
    void dbl (const MouseEvent& e)
    {
        const auto p = toLayout (e.position);
        if (p.x < 52 || p.x > 1228) { ed.setSize (1280, (int) WH); return; }
        const int i = hitTest (p); if (i < 0) return;
        const auto& it = items[(size_t) i];
        if (it.kind == Knob) { const auto* d = defOf (it.pid); if (d) setUser (it.pid, d->df); }
        else if (it.kind == Cv && it.cv == 2)
        {
            const float mx = p.x - it.r.x - 1; const int n = bands(); auto ed2 = edges(); const float pw = GX1 - GX0;
            static const float defs[] = { 120, 1200, 6000 };
            for (int k = 1; k < n; ++k) if (std::abs (mx - fxp (ed2[(size_t) k], GX0, pw)) < 6) { setUser ("m.x" + String (k), defs[k - 1]); return; }
        }
        else if (it.kind == Cv && it.band >= 0 && (it.cv - 5) % 3 == 1) setUser ("b" + String (it.band) + ".thr", -48.0f);
    }
    void wheel (const MouseEvent& e, const MouseWheelDetails& w)
    {
        const int i = hitTest (toLayout (e.position)); if (i < 0) return; const auto& it = items[(size_t) i];
        const float dy = w.isReversed ? -w.deltaY : w.deltaY; if (std::abs (dy) < 1.0e-6f) return;
        if (it.kind == Cv && it.band >= 0 && (it.cv - 5) % 3 == 1)
        {
            const String id = "b" + String (it.band) + ".thr"; setUser (id, get (id) + (dy > 0 ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.5f : 2.0f)); return;
        }
        if (it.kind != Knob) return;
        const auto* d = defOf (it.pid); if (! d) return;
        begin (it.pid);
        set (it.pid, d->isInt ? get (it.pid) + (dy > 0 ? 1.0f : -1.0f) : valOf (it.pid, posOf (it.pid, get (it.pid)) + (dy > 0 ? 1 : -1) * (e.mods.isShiftDown() ? 0.002 : 0.02)));
        end (it.pid);
    }
    void move (const MouseEvent& e)
    {
        const auto p = toLayout (e.position); const int i = hitTest (p);
        if (i != hot) { hot = i; tipTicks = 0; tipText = {}; tipItem = -1; ed.repaint(); }
        tipPos = p;
        if (i >= 0 && interactive (items[(size_t) i])) ed.setMouseCursor (items[(size_t) i].kind == Knob ? MouseCursor::UpDownResizeCursor : MouseCursor::PointingHandCursor);
        else ed.setMouseCursor (MouseCursor::NormalCursor);
    }
    void tick()
    {
        updateSpectrum();
        if (hot >= 0 && tipText.isEmpty() && ++tipTicks > 20) { tipText = items[(size_t) hot].tip; tipItem = hot; }
        if (toastTicks > 0) --toastTicks;
        ed.repaint();
    }
};

// ================================================================================================
ClawShaperEditor::ClawShaperEditor (ClawShaperProcessor& p) : AudioProcessorEditor (p), d (std::make_unique<Impl> (*this, p))
{
    setOpaque (true);
    setSize (1280, 680);
    setResizable (true, true);
    setResizeLimits (640, 340, 2560, 1360);
    getConstrainer()->setFixedAspectRatio (1280.0 / 680.0);
    setWantsKeyboardFocus (true);
    setRate ((bool) p.getSetting (setting::power, false) ? 12 : 30);
}
ClawShaperEditor::~ClawShaperEditor() { stopTimer(); }
void ClawShaperEditor::setRate (int hz) { startTimerHz (hz); }
void ClawShaperEditor::paint (Graphics& g)
{
    const float s = (float) getWidth() / 1280.0f;
    g.addTransform (AffineTransform::scale (s));
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    d->paintAll (g);
}
void ClawShaperEditor::resized() {}
void ClawShaperEditor::timerCallback() { d->tick(); }
void ClawShaperEditor::mouseDown (const MouseEvent& e) { d->down (e); }
void ClawShaperEditor::mouseDrag (const MouseEvent& e) { d->drag (e); }
void ClawShaperEditor::mouseUp (const MouseEvent&) { d->up(); }
void ClawShaperEditor::mouseMove (const MouseEvent& e) { d->move (e); }
void ClawShaperEditor::mouseExit (const MouseEvent&) { d->hot = -1; d->tipText = {}; d->tipItem = -1; repaint(); }
void ClawShaperEditor::mouseDoubleClick (const MouseEvent& e) { d->dbl (e); }
void ClawShaperEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) { d->wheel (e, w); }
bool ClawShaperEditor::keyPressed (const KeyPress& k)
{
    if (k.getTextCharacter() == 'b' || k.getTextCharacter() == 'B') { d->setUser ("m.byp", d->get ("m.byp") > 0.5f ? 0.0f : 1.0f); return true; }
    if (k.getTextCharacter() == '[') { d->stepPreset (-1); return true; }
    if (k.getTextCharacter() == ']') { d->stepPreset (1); return true; }
    return false;
}
