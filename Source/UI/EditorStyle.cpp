// The platform-neutral half of EditorStyle.h: colour arithmetic, the model
// tone mapping and the text-fitting rules. Metrics and drawing come from the
// back end (EditorStyleApple.mm, EditorStyleWin.cpp).
#include "EditorStyle.h"

#include <algorithm>
#include <cmath>

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

// ----------------------------------------------------------------- text
void drawLine(Canvas& canvas, const std::string& value, const Font& font, Colour colour, float x, float baseline,
              float horizontalScale)
{
    if (value.empty() || colour.isTransparent())
        return;
    canvas.drawText(value, font, colour, x, baseline, horizontalScale);
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

void drawFitted(Canvas& canvas, const std::string& value, Rect box, const Font& font, Colour colour,
                unsigned justification, int maximumLines, float minimumHorizontalScale)
{
    if (value.empty() || colour.isTransparent())
        return;
    const float width = box.width;
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
    float y = box.y;
    if (justification & verticallyCentred)
        y += (box.height - blockHeight) * 0.5f;
    else if (justification & bottom)
        y += box.height - blockHeight;
    const float asc = ascent(font);
    for (const auto& line : lines)
    {
        const float lineWidth = stringWidth(font, line.text) * line.scale;
        float x = box.x;
        if (justification & horizontallyCentred)
            x += (width - lineWidth) * 0.5f;
        else if (justification & right)
            x += width - lineWidth;
        drawLine(canvas, line.text, font, colour, x, y + asc, line.scale);
        y += lineHeight;
    }
}

float drawWrapped(Canvas& canvas, const std::string& value, float x, float topEdge, float width, const Font& font,
                  Colour colour, float lineHeight)
{
    const auto lines = wrapLines(value, font, width);
    float y = topEdge;
    for (const auto& line : lines)
    {
        drawLine(canvas, line, font, colour, x, y + ascent(font));
        y += lineHeight;
    }
    return y - topEdge;
}
} // namespace chordengine::ui::style
