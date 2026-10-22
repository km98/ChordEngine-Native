#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chordengine::platform
{
// =====================================================================
// A small JSON value: null, bool, integer, double, string, array, object.
//
// Integers and doubles are kept apart because the licensing contract tells
// a real JSON boolean from a number (`plugin_entitled: 1` is NOT a grant),
// and an object keeps its keys in insertion order so a request body is
// written exactly in the order the code builds it.
//
// Strict parser: RFC 8259 text only, a depth limit, and the whole input
// must be consumed. Invalid text yields std::nullopt, never a partial value.
// =====================================================================
class Json
{
public:
    enum class Type : std::uint8_t { null, boolean, integer, real, string, array, object };
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : type_(Type::boolean), bool_(value) {}
    Json(int value) : type_(Type::integer), int_(value) {}
    Json(std::int64_t value) : type_(Type::integer), int_(value) {}
    Json(double value) : type_(Type::real), real_(value) {}
    Json(const char* value) : type_(Type::string), string_(value != nullptr ? value : "") {}
    Json(std::string value) : type_(Type::string), string_(std::move(value)) {}
    Json(Array value);
    Json(Object value);

    static Json array() { return Json(Array {}); }
    static Json object() { return Json(Object {}); }

    Type type() const noexcept { return type_; }
    bool isNull() const noexcept { return type_ == Type::null; }
    bool isBool() const noexcept { return type_ == Type::boolean; }
    bool isInteger() const noexcept { return type_ == Type::integer; }
    bool isReal() const noexcept { return type_ == Type::real; }
    bool isNumber() const noexcept { return type_ == Type::integer || type_ == Type::real; }
    bool isString() const noexcept { return type_ == Type::string; }
    bool isArray() const noexcept { return type_ == Type::array; }
    bool isObject() const noexcept { return type_ == Type::object; }

    bool asBool() const noexcept { return type_ == Type::boolean && bool_; }
    std::int64_t asInt() const noexcept;
    double asDouble() const noexcept;
    // The string itself; a number or bool is rendered as text, null/array/
    // object yield "".
    std::string asString() const;
    const std::string& stringRef() const noexcept { return string_; }

    const Array& items() const noexcept;
    const Object& members() const noexcept;

    // Object access. A missing key (or a non-object) yields a null value.
    bool has(const std::string& key) const noexcept;
    const Json& operator[](const std::string& key) const noexcept;
    const Json& operator[](const char* key) const noexcept { return (*this)[std::string(key)]; }
    // Sets (replacing an existing key in place, else appending).
    Json& set(const std::string& key, Json value);
    // Removes a key (no-op when absent or not an object).
    void remove(const std::string& key);
    // Mutable access (copy-on-write: other copies are not affected). member()
    // makes this an object and inserts a null value for a missing key;
    // mutableItems() makes this an array.
    Json& member(const std::string& key);
    Array& mutableItems();
    void push(Json value);
    std::size_t size() const noexcept;

    // Compact JSON text (no whitespace).
    std::string dump() const;

    static std::optional<Json> parse(const std::string& text);
    static constexpr int maxDepth = 64;

private:
    Type type_ = Type::null;
    bool bool_ = false;
    std::int64_t int_ = 0;
    double real_ = 0.0;
    std::string string_;
    std::shared_ptr<Array> array_;
    std::shared_ptr<Object> object_;
};

// Writes `text` as a JSON string literal (quotes included).
void appendJsonString(std::string& out, const std::string& text);
} // namespace chordengine::platform
