#include "XmlLite.h"

#include <cstdint>

namespace chordengine::state
{
namespace
{
bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isNameStart(unsigned char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':' || c >= 0x80;
}

bool isNameChar(unsigned char c) noexcept
{
    return isNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

void appendUtf8(std::string& out, std::uint32_t code)
{
    if (code < 0x80)
        out += static_cast<char>(code);
    else if (code < 0x800)
    {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000)
    {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

class Reader
{
public:
    explicit Reader(const std::string& text) : text_(text) {}

    std::optional<XmlElementLite> run()
    {
        // A UTF-8 byte order mark is allowed.
        if (text_.compare(0, 3, "\xEF\xBB\xBF") == 0)
            position_ = 3;
        if (!skipProlog())
            return std::nullopt;
        if (!peek('<'))
            return std::nullopt;
        auto root = parseElement(0, true);
        if (!root.has_value())
            return std::nullopt;
        // Only whitespace, comments and processing instructions may follow.
        if (!skipMisc())
            return std::nullopt;
        if (position_ != text_.size())
            return std::nullopt;
        return root;
    }

private:
    bool peek(char c) const noexcept { return position_ < text_.size() && text_[position_] == c; }
    bool startsWith(const char* prefix) const noexcept
    {
        return text_.compare(position_, std::char_traits<char>::length(prefix), prefix) == 0;
    }
    void skipSpace()
    {
        while (position_ < text_.size() && isSpace(text_[position_]))
            ++position_;
    }

    bool skipUntil(const char* terminator)
    {
        const auto at = text_.find(terminator, position_);
        if (at == std::string::npos)
            return false;
        position_ = at + std::char_traits<char>::length(terminator);
        return true;
    }

    // Whitespace, comments and processing instructions.
    bool skipMisc()
    {
        for (;;)
        {
            skipSpace();
            if (startsWith("<!--"))
            {
                position_ += 4;
                if (!skipUntil("-->"))
                    return false;
            }
            else if (startsWith("<?"))
            {
                position_ += 2;
                if (!skipUntil("?>"))
                    return false;
            }
            else
                return true;
        }
    }

    bool skipProlog()
    {
        if (!skipMisc())
            return false;
        if (startsWith("<!DOCTYPE"))
        {
            if (!skipUntil(">"))
                return false;
            return skipMisc();
        }
        return true;
    }

    std::optional<std::string> parseName()
    {
        const auto start = position_;
        if (position_ >= text_.size() || !isNameStart(static_cast<unsigned char>(text_[position_])))
            return std::nullopt;
        ++position_;
        while (position_ < text_.size() && isNameChar(static_cast<unsigned char>(text_[position_])))
            ++position_;
        return text_.substr(start, position_ - start);
    }

    // Decodes one reference after '&'; appends the character(s).
    bool parseReference(std::string& out)
    {
        const auto end = text_.find(';', position_);
        if (end == std::string::npos || end - position_ > 12)
            return false;
        const auto name = text_.substr(position_, end - position_);
        position_ = end + 1;
        if (name == "amp") { out += '&'; return true; }
        if (name == "lt") { out += '<'; return true; }
        if (name == "gt") { out += '>'; return true; }
        if (name == "quot") { out += '"'; return true; }
        if (name == "apos") { out += '\''; return true; }
        if (name.size() >= 2 && name[0] == '#')
        {
            std::uint32_t code = 0;
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const auto digits = name.substr(hex ? 2 : 1);
            if (digits.empty())
                return false;
            for (const auto c : digits)
            {
                int value = -1;
                if (c >= '0' && c <= '9') value = c - '0';
                else if (hex && c >= 'a' && c <= 'f') value = c - 'a' + 10;
                else if (hex && c >= 'A' && c <= 'F') value = c - 'A' + 10;
                if (value < 0)
                    return false;
                code = code * (hex ? 16u : 10u) + static_cast<std::uint32_t>(value);
                if (code > 0x10FFFF)
                    return false;
            }
            if (code == 0 || (code >= 0xD800 && code <= 0xDFFF))
                return false;
            appendUtf8(out, code);
            return true;
        }
        return false;
    }

    std::optional<std::string> parseAttributeValue()
    {
        if (position_ >= text_.size() || (text_[position_] != '"' && text_[position_] != '\''))
            return std::nullopt;
        const char quote = text_[position_++];
        std::string value;
        while (position_ < text_.size())
        {
            const char c = text_[position_++];
            if (c == quote)
                return value;
            if (c == '<')
                return std::nullopt;
            if (c == '&')
            {
                if (!parseReference(value))
                    return std::nullopt;
                continue;
            }
            value += c;
        }
        return std::nullopt;
    }

    std::optional<XmlElementLite> parseElement(int depth, bool keep)
    {
        if (depth > 32 || !peek('<'))
            return std::nullopt;
        ++position_;
        auto tag = parseName();
        if (!tag.has_value())
            return std::nullopt;

        XmlElementLite element;
        element.tag = *tag;
        for (;;)
        {
            const auto before = position_;
            skipSpace();
            if (startsWith("/>"))
            {
                position_ += 2;
                return element;
            }
            if (peek('>'))
            {
                ++position_;
                break;
            }
            if (position_ == before)
                return std::nullopt; // attributes must be separated by whitespace
            auto name = parseName();
            if (!name.has_value())
                return std::nullopt;
            skipSpace();
            if (!peek('='))
                return std::nullopt;
            ++position_;
            skipSpace();
            auto value = parseAttributeValue();
            if (!value.has_value())
                return std::nullopt;
            if (keep)
                element.attributes.emplace_back(std::move(*name), std::move(*value));
        }

        // Content: text, references, comments, CDATA, PIs and child elements,
        // all skipped, up to the matching end tag.
        for (;;)
        {
            if (position_ >= text_.size())
                return std::nullopt;
            if (startsWith("</"))
            {
                position_ += 2;
                auto closing = parseName();
                if (!closing.has_value() || *closing != element.tag)
                    return std::nullopt;
                skipSpace();
                if (!peek('>'))
                    return std::nullopt;
                ++position_;
                return element;
            }
            if (startsWith("<!--"))
            {
                position_ += 4;
                if (!skipUntil("-->"))
                    return std::nullopt;
                continue;
            }
            if (startsWith("<![CDATA["))
            {
                position_ += 9;
                if (!skipUntil("]]>"))
                    return std::nullopt;
                continue;
            }
            if (startsWith("<?"))
            {
                position_ += 2;
                if (!skipUntil("?>"))
                    return std::nullopt;
                continue;
            }
            if (peek('<'))
            {
                if (!parseElement(depth + 1, false).has_value())
                    return std::nullopt;
                continue;
            }
            if (peek('&'))
            {
                ++position_;
                std::string ignored;
                if (!parseReference(ignored))
                    return std::nullopt;
                continue;
            }
            ++position_;
        }
    }

    const std::string& text_;
    std::size_t position_ = 0;
};

void appendEscaped(std::string& out, const std::string& value)
{
    for (const auto raw : value)
    {
        const auto c = static_cast<unsigned char>(raw);
        switch (c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:
                if (c < 0x20)
                {
                    out += "&#" + std::to_string(c) + ";";
                }
                else
                    out += static_cast<char>(c);
        }
    }
}
} // namespace

bool XmlElementLite::has(const std::string& name) const noexcept
{
    for (const auto& attribute : attributes)
        if (attribute.first == name)
            return true;
    return false;
}

const std::string& XmlElementLite::get(const std::string& name) const noexcept
{
    static const std::string empty;
    for (const auto& attribute : attributes)
        if (attribute.first == name)
            return attribute.second;
    return empty;
}

void XmlElementLite::set(const std::string& name, std::string value)
{
    for (auto& attribute : attributes)
        if (attribute.first == name)
        {
            attribute.second = std::move(value);
            return;
        }
    attributes.emplace_back(name, std::move(value));
}

std::optional<XmlElementLite> parseXmlRoot(const std::string& document)
{
    return Reader(document).run();
}

std::string writeXmlDocument(const XmlElementLite& root)
{
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<" + root.tag;
    for (const auto& [name, value] : root.attributes)
    {
        out += ' ';
        out += name;
        out += "=\"";
        appendEscaped(out, value);
        out += '"';
    }
    out += "/>\n";
    return out;
}
} // namespace chordengine::state
