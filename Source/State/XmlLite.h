#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chordengine::state
{
// =====================================================================
// The minimal XML the host state needs: ONE root element with attributes.
//
// The reader accepts every document a 0.5.x build wrote (an XML
// declaration, attributes wrapped over several lines, CRLF line ends,
// character and entity references) and refuses anything that is not
// well-formed. Child elements, text, comments and processing instructions
// inside the root are skipped (forward compatibility), but must still be
// well-formed.
// =====================================================================
struct XmlElementLite
{
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attributes;

    bool has(const std::string& name) const noexcept;
    // The first attribute of that name ("" when absent).
    const std::string& get(const std::string& name) const noexcept;
    void set(const std::string& name, std::string value);
};

std::optional<XmlElementLite> parseXmlRoot(const std::string& document);

// `<?xml version="1.0" encoding="UTF-8"?>` + the element on one line.
std::string writeXmlDocument(const XmlElementLite& root);
} // namespace chordengine::state
