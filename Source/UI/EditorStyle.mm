#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#include <dlfcn.h>

#include "EditorStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>

namespace chordengine::ui::style
{
// ---------------------------------------------------------------- colour
namespace
{
std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

Colour make(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return Colour((static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16)
                  | (static_cast<std::uint32_t>(g) << 8) | b);
}
} // namespace

Colour Colour::withAlpha(float a) const noexcept
{
    return make(toByte(std::clamp(a, 0.0f, 1.0f) * 255.0f), r(), g(), b());
}

Colour Colour::withMultipliedAlpha(float factor) const noexcept
{
    return withAlpha(alpha() * factor);
}

Colour Colour::brighter(float amount) const noexcept
{
    const float k = 1.0f / (1.0f + std::max(0.0f, amount));
    const auto lift = [k](std::uint8_t c) { return static_cast<std::uint8_t>(255 - k * (255 - c)); };
    return make(static_cast<std::uint8_t>(argb >> 24), lift(r()), lift(g()), lift(b()));
}

Colour Colour::darker(float amount) const noexcept
{
    const float k = 1.0f / (1.0f + std::max(0.0f, amount));
    const auto drop = [k](std::uint8_t c) { return static_cast<std::uint8_t>(k * c); };
    return make(static_cast<std::uint8_t>(argb >> 24), drop(r()), drop(g()), drop(b()));
}

CGColorRef Colour::createCG() const
{
    static CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    const CGFloat components[] = { r() / 255.0, g() / 255.0, b() / 255.0, alpha() };
    return CGColorCreate(space, components);
}

Colour displayTone(std::uint32_t modelColour) noexcept
{
    switch (modelColour)
    {
        case 0xff7fd48a: return sage;
        case 0xffe0b050: return accent;
        case 0xffe05a5a: return terracotta;
        case 0xff8a93a0: return muted;
        case 0xffe8ebef: return text;
        default: return Colour(modelColour);
    }
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
    CGColorRef cg = colour.createCG();
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

void drawLine(CGContextRef context, const std::string& value, const Font& font, Colour colour,
              float x, float baseline, float horizontalScale)
{
    if (value.empty() || colour.isTransparent())
        return;
    CTLineRef line = createLine(value, font, colour);
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, x, baseline);
    CGContextScaleCTM(context, horizontalScale, -1.0);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextSetTextPosition(context, 0, 0);
    CTLineDraw(line, context);
    CGContextRestoreGState(context);
    CFRelease(line);
}

std::vector<std::string> wrapLines(const std::string& value, const Font& font, float width)
{
    std::vector<std::string> lines;
    std::string current;
    std::size_t index = 0;
    while (index <= value.size())
    {
        const auto space = value.find_first_of(" \n", index);
        const auto end = space == std::string::npos ? value.size() : space;
        const auto word = value.substr(index, end - index);
        const auto candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && stringWidth(font, candidate) > width)
        {
            lines.push_back(current);
            current = word;
        }
        else
            current = candidate;
        if (space != std::string::npos && value[space] == '\n')
        {
            lines.push_back(current);
            current.clear();
        }
        if (space == std::string::npos)
            break;
        index = space + 1;
    }
    if (!current.empty())
        lines.push_back(current);
    return lines;
}

namespace
{
std::string ellipsise(const std::string& value, const Font& font, float width)
{
    if (stringWidth(font, value) <= width)
        return value;
    std::string cut = value;
    while (!cut.empty())
    {
        do
            cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80);
        if (stringWidth(font, cut + "...") <= width)
            return cut + "...";
    }
    return {};
}
} // namespace

void drawFitted(CGContextRef context, const std::string& value, CGRect box, const Font& font,
                Colour colour, unsigned justification, int maximumLines, float minimumHorizontalScale)
{
    if (value.empty() || colour.isTransparent())
        return;
    const float width = static_cast<float>(box.size.width);
    const float lineHeight = font.height;
    const float minScale = minimumHorizontalScale <= 0.0f ? 0.7f : minimumHorizontalScale;

    struct Line
    {
        std::string text;
        float scale = 1.0f;
    };
    std::vector<Line> lines;
    const float full = stringWidth(font, value);
    if (full <= width)
        lines.push_back({ value, 1.0f });
    else if (maximumLines <= 1 || full * minScale <= width)
    {
        if (full * minScale <= width)
            lines.push_back({ value, width / full });
        else
            lines.push_back({ ellipsise(value, font, width / minScale), minScale });
    }
    else
    {
        auto wrapped = wrapLines(value, font, width);
        if (static_cast<int>(wrapped.size()) > maximumLines)
        {
            std::string rest;
            for (std::size_t index = static_cast<std::size_t>(maximumLines - 1); index < wrapped.size(); ++index)
                rest += (rest.empty() ? "" : " ") + wrapped[index];
            wrapped.resize(static_cast<std::size_t>(maximumLines - 1));
            wrapped.push_back(rest);
        }
        for (auto& line : wrapped)
        {
            const float lineWidth = stringWidth(font, line);
            if (lineWidth <= width)
                lines.push_back({ line, 1.0f });
            else if (lineWidth * minScale <= width)
                lines.push_back({ line, width / lineWidth });
            else
                lines.push_back({ ellipsise(line, font, width / minScale), minScale });
        }
    }

    const float blockHeight = lineHeight * static_cast<float>(lines.size());
    float y = static_cast<float>(box.origin.y);
    if (justification & verticallyCentred)
        y += (static_cast<float>(box.size.height) - blockHeight) * 0.5f;
    else if (justification & bottom)
        y += static_cast<float>(box.size.height) - blockHeight;
    const float asc = ascent(font);
    for (const auto& line : lines)
    {
        const float lineWidth = stringWidth(font, line.text) * line.scale;
        float x = static_cast<float>(box.origin.x);
        if (justification & horizontallyCentred)
            x += (width - lineWidth) * 0.5f;
        else if (justification & right)
            x += width - lineWidth;
        drawLine(context, line.text, font, colour, x, y + asc, line.scale);
        y += lineHeight;
    }
}

float drawWrapped(CGContextRef context, const std::string& value, float x, float topEdge, float width,
                  const Font& font, Colour colour, float lineHeight)
{
    const auto lines = wrapLines(value, font, width);
    float y = topEdge;
    for (const auto& line : lines)
    {
        drawLine(context, line, font, colour, x, y + ascent(font));
        y += lineHeight;
    }
    return y - topEdge;
}

// ------------------------------------------------------------- shapes
namespace
{
void setFill(CGContextRef context, Colour colour)
{
    CGColorRef cg = colour.createCG();
    CGContextSetFillColorWithColor(context, cg);
    CGColorRelease(cg);
}

void setStroke(CGContextRef context, Colour colour)
{
    CGColorRef cg = colour.createCG();
    CGContextSetStrokeColorWithColor(context, cg);
    CGColorRelease(cg);
}
} // namespace

void fillRect(CGContextRef context, CGRect rect, Colour colour)
{
    if (colour.isTransparent())
        return;
    setFill(context, colour);
    CGContextFillRect(context, rect);
}

void fillRoundedRect(CGContextRef context, CGRect rect, float radius, Colour colour)
{
    if (colour.isTransparent() || rect.size.width <= 0 || rect.size.height <= 0)
        return;
    radius = std::min({ radius, static_cast<float>(rect.size.width) * 0.5f, static_cast<float>(rect.size.height) * 0.5f });
    CGPathRef path = CGPathCreateWithRoundedRect(rect, radius, radius, nullptr);
    setFill(context, colour);
    CGContextAddPath(context, path);
    CGContextFillPath(context);
    CGPathRelease(path);
}

void strokeRoundedRect(CGContextRef context, CGRect rect, float radius, float lineWidth, Colour colour)
{
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

void fillEllipse(CGContextRef context, CGRect rect, Colour colour)
{
    if (colour.isTransparent())
        return;
    setFill(context, colour);
    CGContextFillEllipseInRect(context, rect);
}

void strokeEllipse(CGContextRef context, CGRect rect, float lineWidth, Colour colour)
{
    if (colour.isTransparent())
        return;
    setStroke(context, colour);
    CGContextSetLineWidth(context, lineWidth);
    CGContextStrokeEllipseInRect(context, rect);
}

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

void fillPathVerticalGradient(CGContextRef context, CGPathRef path, CGRect rect, Colour topColour, Colour bottomColour)
{
    CGColorRef colours[] = { topColour.createCG(), bottomColour.createCG() };
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
}

void strokePolyline(CGContextRef context, const std::vector<CGPoint>& points, float lineWidth, Colour colour)
{
    if (points.size() < 2)
        return;
    setStroke(context, colour);
    CGContextSetLineWidth(context, lineWidth);
    CGContextSetLineCap(context, kCGLineCapRound);
    CGContextSetLineJoin(context, kCGLineJoinRound);
    CGContextAddLines(context, points.data(), points.size());
    CGContextStrokePath(context);
}

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
} // namespace chordengine::ui::style
