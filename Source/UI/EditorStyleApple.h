#pragma once

// The Core Text / Core Graphics back end of EditorStyle.h (macOS only).
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>

#include "EditorStyle.h"

namespace chordengine::ui::style
{
// The CTFont for a font (cached; do not release).
CTFontRef ctFont(const Font& font);
// A new CGColor (+1); the caller releases it.
CGColorRef createCGColor(Colour colour);

inline CGRect toCG(Rect rect) { return CGRectMake(rect.x, rect.y, rect.width, rect.height); }
inline Rect fromCG(CGRect rect)
{
    return { static_cast<float>(rect.origin.x), static_cast<float>(rect.origin.y), static_cast<float>(rect.size.width),
             static_cast<float>(rect.size.height) };
}
inline CGPoint toCG(Point point) { return CGPointMake(point.x, point.y); }
inline Point fromCG(CGPoint point) { return { static_cast<float>(point.x), static_cast<float>(point.y) }; }

// A Canvas on a flipped CGContext (y grows downwards).
class CGCanvas final : public Canvas
{
public:
    explicit CGCanvas(CGContextRef context) : context_(context) {}
    CGContextRef context() const noexcept { return context_; }

    void fillRect(Rect rect, Colour colour) override;
    void fillRoundedRect(Rect rect, float radius, Colour colour) override;
    void strokeRoundedRect(Rect rect, float radius, float lineWidth, Colour colour) override;
    void fillEllipse(Rect rect, Colour colour) override;
    void strokeEllipse(Rect rect, float lineWidth, Colour colour) override;
    void fillBottomRoundedGradient(Rect rect, float radius, Colour top, Colour bottom) override;
    void strokePolyline(const std::vector<Point>& points, float lineWidth, Colour colour) override;
    void drawText(const std::string& text, const Font& font, Colour colour, float x, float baseline,
                  float horizontalScale) override;
    void drawMusicProdMark(Rect target, Colour tone, float opacity) override;
    void save() override;
    void restore() override;
    void translate(float dx, float dy) override;
    void clipTo(Rect rect) override;
    void beginLayer(float opacity) override;
    void endLayer() override;

private:
    CGContextRef context_;
};
} // namespace chordengine::ui::style
