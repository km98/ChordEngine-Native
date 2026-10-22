#pragma once

// Shared helpers for the ChordEngine test programs (no third-party code).

#include "Platform/Json.h"
#include "Platform/Platform.h"

#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace chordengine::test
{
[[noreturn]] inline void fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(EXIT_FAILURE);
}

inline void require(bool condition, const std::string& message)
{
    if (!condition)
        fail(message);
}

inline std::string str(long long value) { return std::to_string(value); }

// A fresh, empty directory under the system temporary folder.
inline std::filesystem::path freshDirectory(const std::string& suite, const std::string& name)
{
    auto directory = platform::temporaryDirectory() / suite / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return directory;
}

inline std::string readText(const std::filesystem::path& path)
{
    auto bytes = platform::readFile(path);
    require(bytes.has_value(), "readable: " + path.string());
    return *bytes;
}

inline platform::Json parseJson(const std::string& text)
{
    auto parsed = platform::Json::parse(text);
    require(parsed.has_value(), "valid JSON");
    return *parsed;
}

// Builds a JSON object in field order.
inline platform::Json object(std::initializer_list<std::pair<const char*, platform::Json>> fields)
{
    auto result = platform::Json::object();
    for (const auto& field : fields)
        result.set(field.first, field.second);
    return result;
}

inline platform::Json array(std::initializer_list<platform::Json> items)
{
    auto result = platform::Json::array();
    for (const auto& item : items)
        result.push(item);
    return result;
}

// Strict accessors for fixture documents.
inline const platform::Json& field(const platform::Json& value, const std::string& key)
{
    require(value.isObject(), "expected a JSON object while reading " + key);
    require(value.has(key), "missing JSON field: " + key);
    return value[key];
}

inline const platform::Json& at(const platform::Json& value, std::size_t index, const std::string& name)
{
    require(value.isArray() && index < value.items().size(), "expected array entry: " + name);
    return value.items()[index];
}

inline int integer(const platform::Json& value, const std::string& what = "fixture value")
{
    require(value.isInteger(), "expected an integer " + what);
    return static_cast<int>(value.asInt());
}
} // namespace chordengine::test
