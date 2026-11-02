#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// Colours and fonts of the faceplate. Values are the mockup's CSS tokens (look C is the default).
namespace theme
{
using juce::Colour;
struct Look { Colour bg, plate, plate2, line, ink, dim, title, accent, disp, yellow; };
inline const Look& look (char c)
{
    static const Look C { Colour (0xff0b0b0d), Colour (0xff16171a), Colour (0xff1e2024), Colour (0xff33363d), Colour (0xfff4f5f7),
                          Colour (0xffa9adb8), Colour (0xffe8e9ee), Colour (0xff02EBBA), Colour (0xff06080a), Colour (0xffFFD100) };
    static const Look A { Colour (0xff161638), Colour (0xff1c1c4a), Colour (0xff2C2C62), Colour (0xff3d4a66), Colour (0xffe8e6e1),
                          Colour (0xffa8a4c8), Colour (0xffE1CBF4), Colour (0xffFFA600), Colour (0xff05060d), Colour (0xffFFA600) };
    static const Look B { Colour (0xff0b0b0d), Colour (0xff15161a), Colour (0xff1c1d22), Colour (0xff2b2d34), Colour (0xfff4f5f7),
                          Colour (0xffa9adb8), Colour (0xffE1CBF4), Colour (0xffA43DFF), Colour (0xff06080a), Colour (0xffFFD100) };
    return c == 'A' ? A : c == 'B' ? B : C;
}
inline const Colour red { 0xffFF1383 }, blue { 0xff6CC9F5 }, purple { 0xffA43DFF }, sel { 0xff241238 }, selb { 0xffCD92FE },
                    pink { 0xffFF7BB1 }, lilac { 0xffE1CBF4 }, green { 0xff02EBBA }, orange { 0xffF57F01 };
enum class FontKind { regular, semi, bold, brand, mono };
inline juce::Font font (FontKind k, float px)
{
    static const auto regular = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedMedium_ttf, BinaryData::BarlowCondensedMedium_ttfSize);
    static const auto semi    = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize);
    static const auto bold    = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize);
    static const auto brand   = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedExtraBoldItalic_ttf, BinaryData::BarlowCondensedExtraBoldItalic_ttfSize);
    static const auto mono    = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    const auto& tf = k == FontKind::regular ? regular : k == FontKind::semi ? semi : k == FontKind::bold ? bold : k == FontKind::brand ? brand : mono;
    // CSS pixel size (em) -> JUCE height in pixels: JUCE's height is ascent + descent, which is 1.2 em for
    // Barlow Condensed and 1.32 em for JetBrains Mono (hhea metrics of the bundled files).
    return juce::Font (juce::FontOptions (tf).withHeight (px * (k == FontKind::mono ? 1.32f : 1.2f)));
}
} // namespace theme
