/**
 * json_builder.h — Minimal JSON builder for internal use only.
 *
 * No external deps. Builds the fixed-schema JSON payloads the backend expects.
 * Not a general-purpose JSON library — only what deviceai-commons needs.
 */
#pragma once

#include <string>
#include <sstream>
#include <cstdint>

namespace dai {
namespace json {

// Escape a string value for embedding in JSON
inline std::string escape(const std::string& s) {
    std::ostringstream out;
    for (char c : s) {
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:   out << c;      break;
        }
    }
    return out.str();
}

class ObjectBuilder {
public:
    ObjectBuilder& str(const std::string& key, const std::string& val) {
        append_key(key);
        buf_ << '"' << escape(val) << '"';
        return *this;
    }
    ObjectBuilder& str(const std::string& key, const char* val) {
        return str(key, val ? std::string(val) : std::string());
    }
    ObjectBuilder& i64(const std::string& key, int64_t val) {
        append_key(key);
        buf_ << val;
        return *this;
    }
    ObjectBuilder& i32(const std::string& key, int val) {
        append_key(key);
        buf_ << val;
        return *this;
    }
    ObjectBuilder& f32(const std::string& key, float val) {
        append_key(key);
        buf_ << val;
        return *this;
    }
    ObjectBuilder& boolean(const std::string& key, bool val) {
        append_key(key);
        buf_ << (val ? "true" : "false");
        return *this;
    }
    ObjectBuilder& raw(const std::string& key, const std::string& json_val) {
        append_key(key);
        buf_ << json_val;
        return *this;
    }
    std::string build() const {
        return "{" + buf_.str() + "}";
    }
private:
    std::ostringstream buf_;
    bool first_ = true;
    void append_key(const std::string& key) {
        if (!first_) buf_ << ',';
        buf_ << '"' << key << '"' << ':';
        first_ = false;
    }
};

class ArrayBuilder {
public:
    ArrayBuilder& item(const std::string& json_val) {
        if (!first_) buf_ << ',';
        buf_ << json_val;
        first_ = false;
        return *this;
    }
    std::string build() const {
        return "[" + buf_.str() + "]";
    }
private:
    std::ostringstream buf_;
    bool first_ = true;
};

} // namespace json
} // namespace dai
