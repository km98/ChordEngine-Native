#pragma once

// =====================================================================
// The editor's visual vocabulary (0.5.0 redesign, Docs/UI_REDESIGN_0.5.0.md):
// the palette, the faces, and text drawing with the metric rules the 0.5
// editor used, so the AppKit editor sets type the same way.
//
//   * A font "height" is ascent + descent (not the point size), as the 0.5
//     editor specified every size; kerning is a fraction of that height.
//   * Fitted text: one line when it fits; otherwise squeezed horizontally down
//     to a minimum scale, wrapped onto as many lines as the box holds, and
//     ellipsised only as a last resort.
// =====================================================================

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace chordengine::ui::style
{
// ---------------------------------------------------------------- colour
struct Colour
{
    std::uint32_t argb = 0;

    constexpr Colour() = default;
    constexpr explicit Colour(std::uint32_t value) : argb(value) {}

    float alpha() const noexcept { return static_cast<float>((argb >> 24) & 0xff) / 255.0f; }
    std::uint8_t r() const noexcept { return static_cast<std::uint8_t>((argb >> 16) & 0xff); }
    std::uint8_t g() const noexcept { return static_cast<std::uint8_t>((argb >> 8) & 0xff); }
    std::uint8_t b() const noexcept { return static_cast<std::uint8_t>(argb & 0xff); }
    bool isTransparent() const noexcept { return (argb >> 24) == 0; }

    Colour withAlpha(float a) const noexcept;
    Colour withMultipliedAlpha(float factor) const noexcept;
    Colour brighter(float amount = 0.4f) const noexcept;
    Colour darker(float amount = 0.4f) const noexcept;
    // A new CGColor (+1); the caller releases it.
    CGColorRef createCG() const;
};

inline constexpr Colour transparent { 0x00000000 };
inline constexpr Colour white { 0xffffffff };
inline constexpr Colour black { 0xff000000 };
inline constexpr Colour background { 0xff0f0f11 };
inline constexpr Colour page { 0xff161618 };
inline constexpr Colour panel { 0xff1a1a1d };
inline constexpr Colour inset { 0xff131315 };
inline constexpr Colour raised { 0xff232326 };
inline constexpr Colour raisedHover { 0xff2a2a2e };
inline constexpr Colour track { 0xff303035 };
inline constexpr Colour hairline { 0xff2a2a2e };
inline constexpr Colour border { 0xff36363b };
inline constexpr Colour text { 0xffece7de };
inline constexpr Colour textBright { 0xfff7f3ec };
inline constexpr Colour muted { 0xffa6a096 };
inline constexpr Colour dim { 0xff7c776e };
inline constexpr Colour faint { 0xff5a5650 };
inline constexpr Colour accent { 0xffd8a55e };
inline constexpr Colour accentSoft { 0xff3a3023 };
inline constexpr Colour onAccent { 0xff1b1610 };
inline constexpr Colour terracotta { 0xffd06a50 };
inline constexpr Colour sage { 0xffa3c39a };
inline constexpr Colour pianoBed { 0xff121214 };
inline constexpr Colour whiteKey { 0xffe9e4da };
inline constexpr Colour whiteKeyShade { 0xffd9d3c7 };
inline constexpr Colour blackKey { 0xff18181b };
inline constexpr Colour blackKeyTop { 0xff26262a };
inline constexpr Colour chordWhiteKey { 0xffd8a55e };
inline constexpr Colour chordBlackKey { 0xffb9884a };
inline constexpr Colour triggerWhiteKey { 0xffd06a50 };
inline constexpr Colour triggerBlackKey { 0xffb4553e };
inline constexpr Colour whiteKeyLabel { 0xff8a8478 };
inline constexpr Colour previewWhite { 0xffe6d3b2 };
inline constexpr Colour previewBlack { 0xff5a4a33 };

// The licensing and update models publish semantic ARGB values; the editor
// maps them onto the palette for display.
Colour displayTone(std::uint32_t modelColour) noexcept;

// ----------------------------------------------------------------- faces
enum class Face : std::uint8_t
{
    regular,   // Inter Regular (bundled, OFL)
    semibold,  // Inter SemiBold (bundled, OFL)
    caption,   // Schibsted Grotesk SemiBold (bundled, OFL)
    display,   // Baskerville / Georgia (system serif)
    wordmark,  // Snell Roundhand Bold (system script)
    mono       // the system monospaced face
};

struct Font
{
    Face face = Face::regular;
    float height = 12.0f;  // ascent + descent, in points
    float kerning = 0.0f;  // extra tracking as a fraction of the height
};

inline Font uiFont(float height, bool bold = false, float kerning = 0.0f)
{
    return { bold ? Face::semibold : Face::regular, height, kerning };
}
inline Font captionFont(float height, float kerning) { return { Face::caption, height, kerning }; }
inline Font displayFont(float height, float kerning = 0.0f) { return { Face::display, height, kerning }; }

// Captions: small letter-spaced capitals.
inline constexpr float captionSize = 8.5f;
inline constexpr float captionKerning = 0.12f;

// The CTFont for a font (cached; do not release).
CTFontRef ctFont(const Font& font);
float ascent(const Font& font);
float descent(const Font& font);
float stringWidth(const Font& font, const std::string& text);

// Where the editor's bundled resources live: <bundle>/Contents/Resources,
// else a "Resources" folder next to the executable, else the source tree's
// Resources (development tools). A file is looked up there and in Fonts/.
std::filesystem::path resourceFile(const std::string& name);

// ----------------------------------------------------------------- text
enum Justification : unsigned
{
    left = 1, right = 2, horizontallyCentred = 4,
    top = 8, bottom = 16, verticallyCentred = 32,
    centredLeft = left | verticallyCentred,
    centredRight = right | verticallyCentred,
    centred = horizontallyCentred | verticallyCentred,
    centredTop = horizontallyCentred | top,
    topLeft = left | top
};

// One line at a baseline (flipped context: y grows downwards).
void drawLine(CGContextRef context, const std::string& text, const Font& font, Colour colour,
              float x, float baseline, float horizontalScale = 1.0f);

// Fitted text in a box (see the header comment).
void drawFitted(CGContextRef context, const std::string& text, CGRect box, const Font& font,
                Colour colour, unsigned justification, int maximumLines, float minimumHorizontalScale);

// Word-wrapped text (no squeeze), top-aligned at `top`; returns the height used.
float drawWrapped(CGContextRef context, const std::string& text, float x, float top, float width,
                  const Font& font, Colour colour, float lineHeight);
std::vector<std::string> wrapLines(const std::string& text, const Font& font, float width);

// ------------------------------------------------------------- shapes
void fillRect(CGContextRef context, CGRect rect, Colour colour);
void fillRoundedRect(CGContextRef context, CGRect rect, float radius, Colour colour);
void strokeRoundedRect(CGContextRef context, CGRect rect, float radius, float lineWidth, Colour colour);
void fillEllipse(CGContextRef context, CGRect rect, Colour colour);
void strokeEllipse(CGContextRef context, CGRect rect, float lineWidth, Colour colour);
// Rounded only at the two bottom corners (piano keys).
CGPathRef createBottomRoundedPath(CGRect rect, float radius);
void fillPathVerticalGradient(CGContextRef context, CGPathRef path, CGRect rect, Colour top, Colour bottom);
void strokePolyline(CGContextRef context, const std::vector<CGPoint>& points, float lineWidth, Colour colour);

// The Music-Prod mark: the bundled artwork re-tinted to one tone (alpha from
// luminance), cached. Null when the artwork is missing.
CGImageRef musicProdMark(Colour tone);
} // namespace chordengine::ui::style
