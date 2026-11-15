#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#include <dlfcn.h>

#include "EditorStyleApple.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>

namespace chordengine::ui::style
{
namespace
{
std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}
} // namespace

CGColorRef createCGColor(Colour colour)
{
    static CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    const CGFloat components[] = { colour.r() / 255.0, colour.g() / 255.0, colour.b() / 255.0, colour.alpha() };
    return CGColorCreate(space, components);
}

// ----------------------------------------------------------------- faces
std::filesystem::path resourceFile(const std::string& name)
{
    static const std::vector<std::filesystem::path> roots = []
    {
        std::vector<std::filesystem::path> found;
        Dl_info info {};
        if (dladdr(reinterpret_cast<const void*>(&resourceFile), &info) != 0 && info.dli_fname != nullptr)
        {
            const std::filesystem::path binary(info.dli_fname);
            const auto contents = binary.parent_path().parent_path();
            if (binary.parent_path().filename() == "MacOS")
                found.push_back(contents / "Resources");
            found.push_back(binary.parent_path() / "Resources");
        }
#ifdef CHORDENGINE_RESOURCE_DIR
        found.emplace_back(CHORDENGINE_RESOURCE_DIR);
#endif
        return found;
    }();
    std::error_code error;
    for (const auto& root : roots)
        for (const auto& candidate : { root / name, root / "Fonts" / name })
            if (std::filesystem::exists(candidate, error))
                return candidate;
    return {};
}

namespace
{
CTFontDescriptorRef bundledDescriptor(const char* fileName)
{
    // Process-private: the face is read from the bundle; nothing is installed.
    const auto path = resourceFile(fileName);
    if (path.empty())
        return nullptr;
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef) url);
    if (descriptors == nullptr || CFArrayGetCount(descriptors) == 0)
    {
        if (descriptors != nullptr)
            CFRelease(descriptors);
        return nullptr;
    }
    auto descriptor = static_cast<CTFontDescriptorRef>(CFRetain(CFArrayGetValueAtIndex(descriptors, 0)));
    CFRelease(descriptors);
    return descriptor;
}

CTFontRef baseFont(Face face)
{
    static std::array<CTFontRef, 6> fonts {};
    static std::once_flag once;
    std::call_once(once, []
    {
        const auto fromFile = [](const char* file, const char* fallback, CTFontSymbolicTraits traits) -> CTFontRef
        {
            if (auto descriptor = bundledDescriptor(file))
            {
                CTFontRef font = CTFontCreateWithFontDescriptor(descriptor, 100.0, nullptr);
                CFRelease(descriptor);
                if (font != nullptr)
                    return font;
            }
            CTFontRef system = CTFontCreateWithName(CFStringCreateWithCString(nullptr, fallback, kCFStringEncodingUTF8), 100.0, nullptr);
            if (traits != 0)
                if (CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(system, 100.0, nullptr, traits, traits))
                    return styled;
            return system;
        };
        const auto named = [](std::initializer_list<const char*> names) -> CTFontRef
        {
            for (const auto* name : names)
            {
                CFStringRef cfName = CFStringCreateWithCString(nullptr, name, kCFStringEncodingUTF8);
                CTFontRef font = CTFontCreateWithName(cfName, 100.0, nullptr);
                CFRelease(cfName);
                if (font == nullptr)
                    continue;
                CFStringRef postScript = CTFontCopyPostScriptName(font);
                const bool exact = postScript != nullptr
                    && [(__bridge NSString*) postScript hasPrefix:[[NSString stringWithUTF8String:name]
                                                                      stringByReplacingOccurrencesOfString:@" "
                                                                                                withString:@""]];
                if (postScript != nullptr)
                    CFRelease(postScript);
                if (exact)
                    return font;
                CFRelease(font);
            }
            return CTFontCreateWithName(CFSTR("Times New Roman"), 100.0, nullptr);
        };
        fonts[0] = fromFile("Inter-Regular.ttf", "Helvetica Neue", 0);
        fonts[1] = fromFile("Inter-SemiBold.ttf", "Helvetica Neue", kCTFontBoldTrait);
        fonts[2] = fromFile("SchibstedGrotesk-SemiBold.ttf", "Helvetica Neue", kCTFontBoldTrait);
        // The display serif and the script wordmark ship with the system, as
        // in 0.5: Baskerville (else Georgia), Snell Roundhand Bold.
        fonts[3] = named({ "Baskerville", "Georgia" });
        fonts[4] = named({ "SnellRoundhand-Bold", "Snell Roundhand", "Baskerville-Italic" });
        fonts[5] = CTFontCreateUIFontForLanguage(kCTFontUIFontUserFixedPitch, 100.0, nullptr);
    });
    return fonts[static_cast<std::size_t>(face)];
}

// Ascent + descent of a face per point.
float heightPerPoint(Face face)
{
    CTFontRef font = baseFont(face);
    return static_cast<float>((CTFontGetAscent(font) + CTFontGetDescent(font)) / CTFontGetSize(font));
}

CFAttributedStringRef createAttributed(const std::string& value, const Font& font, Colour colour)
{
    CFStringRef string = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(value.data()),
                                                 static_cast<CFIndex>(value.size()), kCFStringEncodingUTF8, false);
    if (string == nullptr)
        string = CFStringCreateWithCString(nullptr, "", kCFStringEncodingUTF8);
    CGColorRef cg = createCGColor(colour);
    const CGFloat kern = font.kerning * font.height;
    CFNumberRef kernNumber = CFNumberCreate(nullptr, kCFNumberCGFloatType, &kern);
    const void* keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName, kCTKernAttributeName };
    const void* values[] = { ctFont(font), cg, kernNumber };
    CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, font.kerning != 0.0f ? 3 : 2,
                                                    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attributed = CFAttributedStringCreate(nullptr, string, attributes);
    CFRelease(attributes);
    CFRelease(kernNumber);
    CGColorRelease(cg);
    CFRelease(string);
    return attributed;
}

CTLineRef createLine(const std::string& value, const Font& font, Colour colour)
{
    CFAttributedStringRef attributed = createAttributed(value, font, colour);
    CTLineRef line = CTLineCreateWithAttributedString(attributed);
    CFRelease(attributed);
    return line;
}
} // namespace

CTFontRef ctFont(const Font& font)
{
    static std::map<std::pair<int, int>, CTFontRef> cache;
    const float pointSize = std::max(1.0f, font.height / heightPerPoint(font.face));
    const auto key = std::make_pair(static_cast<int>(font.face), static_cast<int>(std::lround(pointSize * 100.0f)));
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;
    CTFontRef sized = CTFontCreateCopyWithAttributes(baseFont(font.face), pointSize, nullptr, nullptr);
    cache.emplace(key, sized);
    return sized;
}

float ascent(const Font& font)
{
    return static_cast<float>(CTFontGetAscent(ctFont(font)));
}

float descent(const Font& font)
{
    return static_cast<float>(CTFontGetDescent(ctFont(font)));
}

float stringWidth(const Font& font, const std::string& value)
{
    if (value.empty())
        return 0.0f;
    CTLineRef line = createLine(value, font, text);
    const double width = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    CFRelease(line);
    // Trailing tracking is not part of the ink.
    return static_cast<float>(width);
}

Rect inkBounds(const Font& font, const std::string& value)
{
    if (value.empty())
        return {};
    CTLineRef line = createLine(value, font, text);
    const CGRect ink = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);
    CFRelease(line);
    if (CGRectIsEmpty(ink))
        return {};
    return fromCG(ink);
}

// ------------------------------------------------------------- shapes
namespace
{
void setFill(CGContextRef context, Colour colour)
{
    CGColorRef cg = createCGColor(colour);
    CGContextSetFillColorWithColor(context, cg);
    CGColorRelease(cg);
}

void setStroke(CGContextRef context, Colour colour)
{
    CGColorRef cg = createCGColor(colour);
    CGContextSetStrokeColorWithColor(context, cg);
    CGColorRelease(cg);
}
} // namespace

void CGCanvas::fillRect(Rect area, Colour colour)
{
    if (colour.isTransparent())
        return;
    setFill(context_, colour);
    CGContextFillRect(context_, toCG(area));
}

void CGCanvas::fillRoundedRect(Rect area, float radius, Colour colour)
{
    CGContextRef context = context_;
    const CGRect rect = toCG(area);
    if (colour.isTransparent() || rect.size.width <= 0 || rect.size.height <= 0)
        return;
    radius = std::min({ radius, static_cast<float>(rect.size.width) * 0.5f, static_cast<float>(rect.size.height) * 0.5f });
    CGPathRef path = CGPathCreateWithRoundedRect(rect, radius, radius, nullptr);
    setFill(context, colour);
    CGContextAddPath(context, path);
    CGContextFillPath(context);
    CGPathRelease(path);
}

void CGCanvas::strokeRoundedRect(Rect area, float radius, float lineWidth, Colour colour)
{
    CGContextRef context = context_;
    const CGRect rect = toCG(area);
    if (colour.isTransparent() || rect.size.width <= 0 || rect.size.height <= 0)
        return;
    radius = std::min({ radius, static_cast<float>(rect.size.width) * 0.5f, static_cast<float>(rect.size.height) * 0.5f });
    CGPathRef path = CGPathCreateWithRoundedRect(rect, radius, radius, nullptr);
    setStroke(context, colour);
    CGContextSetLineWidth(context, lineWidth);
    CGContextAddPath(context, path);
    CGContextStrokePath(context);
    CGPathRelease(path);
}

void CGCanvas::fillEllipse(Rect area, Colour colour)
{
    CGContextRef context = context_;
    const CGRect rect = toCG(area);
    if (colour.isTransparent())
        return;
    setFill(context, colour);
    CGContextFillEllipseInRect(context, rect);
}

void CGCanvas::strokeEllipse(Rect area, float lineWidth, Colour colour)
{
    CGContextRef context = context_;
    const CGRect rect = toCG(area);
    if (colour.isTransparent())
        return;
    setStroke(context, colour);
    CGContextSetLineWidth(context, lineWidth);
    CGContextStrokeEllipseInRect(context, rect);
}

namespace
{
CGPathRef createBottomRoundedPath(CGRect rect, float radius)
{
    CGMutablePathRef path = CGPathCreateMutable();
    const CGFloat x0 = CGRectGetMinX(rect), x1 = CGRectGetMaxX(rect);
    const CGFloat y0 = CGRectGetMinY(rect), y1 = CGRectGetMaxY(rect);
    const CGFloat r = std::min<CGFloat>({ radius, rect.size.width * 0.5, rect.size.height * 0.5 });
    CGPathMoveToPoint(path, nullptr, x0, y0);
    CGPathAddLineToPoint(path, nullptr, x1, y0);
    CGPathAddLineToPoint(path, nullptr, x1, y1 - r);
    CGPathAddArcToPoint(path, nullptr, x1, y1, x1 - r, y1, r);
    CGPathAddLineToPoint(path, nullptr, x0 + r, y1);
    CGPathAddArcToPoint(path, nullptr, x0, y1, x0, y1 - r, r);
    CGPathCloseSubpath(path);
    return path;
}

} // namespace

void CGCanvas::fillBottomRoundedGradient(Rect area, float radius, Colour topColour, Colour bottomColour)
{
    CGContextRef context = context_;
    const CGRect rect = toCG(area);
    CGPathRef path = createBottomRoundedPath(rect, radius);
    CGColorRef colours[] = { createCGColor(topColour), createCGColor(bottomColour) };
    CFArrayRef array = CFArrayCreate(nullptr, (const void**) colours, 2, &kCFTypeArrayCallBacks);
    static CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGGradientRef gradient = CGGradientCreateWithColors(space, array, nullptr);
    CGContextSaveGState(context);
    CGContextAddPath(context, path);
    CGContextClip(context);
    CGContextDrawLinearGradient(context, gradient, CGPointMake(rect.origin.x, CGRectGetMinY(rect)),
                                CGPointMake(rect.origin.x, CGRectGetMaxY(rect)), 0);
    CGContextRestoreGState(context);
    CGGradientRelease(gradient);
    CFRelease(array);
    CGColorRelease(colours[0]);
    CGColorRelease(colours[1]);
    CGPathRelease(path);
}

void CGCanvas::strokePolyline(const std::vector<Point>& vertices, float lineWidth, Colour colour)
{
    CGContextRef context = context_;
    if (vertices.size() < 2)
        return;
    std::vector<CGPoint> points;
    points.reserve(vertices.size());
    for (const auto& vertex : vertices)
        points.push_back(toCG(vertex));
    setStroke(context, colour);
    CGContextSetLineWidth(context, lineWidth);
    CGContextSetLineCap(context, kCGLineCapRound);
    CGContextSetLineJoin(context, kCGLineJoinRound);
    CGContextAddLines(context, points.data(), points.size());
    CGContextStrokePath(context);
}

namespace
{
CGImageRef musicProdMark(Colour tone)
{
    static std::map<std::uint32_t, CGImageRef> cache;
    if (auto found = cache.find(tone.argb); found != cache.end())
        return found->second;

    CGImageRef result = nullptr;
    const auto path = resourceFile("MusicProdLogo.png");
    if (!path.empty())
    {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef) url, nullptr);
        CGImageRef artwork = source != nullptr ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
        if (artwork != nullptr)
        {
            const auto width = CGImageGetWidth(artwork);
            const auto height = CGImageGetHeight(artwork);
            std::vector<std::uint8_t> pixels(width * height * 4);
            static CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            CGContextRef bitmap = CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, space,
                                                        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
            CGContextDrawImage(bitmap, CGRectMake(0, 0, width, height), artwork);
            // Alpha from luminance keeps the exact letterforms and drops the
            // artwork's near-black ground.
            for (std::size_t index = 0; index < width * height; ++index)
            {
                auto* p = &pixels[index * 4];
                const float a = p[3] / 255.0f;
                const float red = a > 0 ? p[0] / 255.0f / a : 0.0f;
                const float green = a > 0 ? p[1] / 255.0f / a : 0.0f;
                const float blue = a > 0 ? p[2] / 255.0f / a : 0.0f;
                const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
                const float alpha = std::clamp((luminance - 0.08f) / 0.84f, 0.0f, 1.0f) * a * tone.alpha();
                p[0] = toByte(tone.r() * alpha);
                p[1] = toByte(tone.g() * alpha);
                p[2] = toByte(tone.b() * alpha);
                p[3] = toByte(alpha * 255.0f);
            }
            result = CGBitmapContextCreateImage(bitmap);
            CGContextRelease(bitmap);
            CGImageRelease(artwork);
        }
        if (source != nullptr)
            CFRelease(source);
    }
    cache.emplace(tone.argb, result);
    return result;
}
} // namespace

float musicProdMarkAspect()
{
    CGImageRef mark = musicProdMark(muted);
    if (mark == nullptr || CGImageGetHeight(mark) == 0)
        return 0.0f;
    return static_cast<float>(CGImageGetWidth(mark)) / static_cast<float>(CGImageGetHeight(mark));
}

void CGCanvas::drawMusicProdMark(Rect area, Colour tone, float opacity)
{
    CGImageRef mark = musicProdMark(tone);
    if (mark == nullptr)
        return;
    const CGRect target = toCG(area);
    CGContextSaveGState(context_);
    CGContextSetAlpha(context_, opacity);
    CGContextTranslateCTM(context_, target.origin.x, CGRectGetMaxY(target));
    CGContextScaleCTM(context_, 1.0, -1.0);
    CGContextSetInterpolationQuality(context_, kCGInterpolationHigh);
    CGContextDrawImage(context_, CGRectMake(0, 0, target.size.width, target.size.height), mark);
    CGContextRestoreGState(context_);
}

void CGCanvas::drawText(const std::string& value, const Font& font, Colour colour, float x, float baseline,
                        float horizontalScale)
{
    if (value.empty() || colour.isTransparent())
        return;
    CTLineRef line = createLine(value, font, colour);
    CGContextSaveGState(context_);
    CGContextTranslateCTM(context_, x, baseline);
    CGContextScaleCTM(context_, horizontalScale, -1.0);
    CGContextSetTextMatrix(context_, CGAffineTransformIdentity);
    CGContextSetTextPosition(context_, 0, 0);
    CTLineDraw(line, context_);
    CGContextRestoreGState(context_);
    CFRelease(line);
}

void CGCanvas::save()
{
    CGContextSaveGState(context_);
}

void CGCanvas::restore()
{
    CGContextRestoreGState(context_);
}

void CGCanvas::translate(float dx, float dy)
{
    CGContextTranslateCTM(context_, dx, dy);
}

void CGCanvas::clipTo(Rect rect)
{
    CGContextClipToRect(context_, toCG(rect));
}

void CGCanvas::beginLayer(float opacity)
{
    CGContextSaveGState(context_);
    CGContextSetAlpha(context_, opacity);
    CGContextBeginTransparencyLayer(context_, nullptr);
}

void CGCanvas::endLayer()
{
    CGContextEndTransparencyLayer(context_);
    CGContextRestoreGState(context_);
}
} // namespace chordengine::ui::style
