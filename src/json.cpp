#include "json.h"

#include <cstdlib>
#include <cstring>

static const Json kNull;

const Json& Json::operator[](const char* key) const {
  if (type_ != Object) return kNull;
  for (size_t i = 0; i < keys_.size(); ++i)
    if (keys_[i] == key) return items_[i];
  return kNull;
}

const Json& Json::operator[](size_t index) const {
  return type_ == Array && index < items_.size() ? items_[index] : kNull;
}

class JsonParser {
 public:
  explicit JsonParser(const std::string& s) : p_(s.c_str()), end_(s.c_str() + s.size()) {
    if (end_ - p_ >= 3 && (unsigned char)p_[0] == 0xEF && (unsigned char)p_[1] == 0xBB && (unsigned char)p_[2] == 0xBF)
      p_ += 3;
  }

  bool parseDocument(Json& out) {
    if (!value(out, 0)) return false;
    ws();
    return p_ == end_;
  }

 private:
  const char* p_;
  const char* end_;

  void ws() { while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_; }

  bool literal(const char* word) {
    size_t n = strlen(word);
    if ((size_t)(end_ - p_) < n || strncmp(p_, word, n) != 0) return false;
    p_ += n;
    return true;
  }

  static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
      out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    } else {
      out += (char)(0xF0 | cp >> 18); out += (char)(0x80 | (cp >> 12 & 0x3F));
      out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    }
  }

  bool hex4(unsigned& out) {
    if (end_ - p_ < 4) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) {
      char c = *p_++;
      out <<= 4;
      if (c >= '0' && c <= '9') out |= c - '0';
      else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
      else return false;
    }
    return true;
  }

  bool string(std::string& out) {
    if (p_ >= end_ || *p_ != '"') return false;
    ++p_;
    while (p_ < end_ && *p_ != '"') {
      char c = *p_++;
      if (c != '\\') { out += c; continue; }
      if (p_ >= end_) return false;
      char e = *p_++;
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
            p_ += 2;
            unsigned lo;
            if (!hex4(lo)) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          appendUtf8(out, cp);
          break;
        }
        default: return false;
      }
    }
    if (p_ >= end_) return false;
    ++p_;
    return true;
  }

  bool value(Json& out, int depth) {
    if (depth > 64) return false;
    ws();
    if (p_ >= end_) return false;
    char c = *p_;
    if (c == '{') {
      ++p_;
      out.type_ = Json::Object;
      ws();
      if (p_ < end_ && *p_ == '}') { ++p_; return true; }
      for (;;) {
        ws();
        std::string key;
        if (!string(key)) return false;
        ws();
        if (p_ >= end_ || *p_++ != ':') return false;
        Json v;
        if (!value(v, depth + 1)) return false;
        out.keys_.push_back(std::move(key));
        out.items_.push_back(std::move(v));
        ws();
        if (p_ < end_ && *p_ == ',') { ++p_; continue; }
        if (p_ < end_ && *p_ == '}') { ++p_; return true; }
        return false;
      }
    }
    if (c == '[') {
      ++p_;
      out.type_ = Json::Array;
      ws();
      if (p_ < end_ && *p_ == ']') { ++p_; return true; }
      for (;;) {
        Json v;
        if (!value(v, depth + 1)) return false;
        out.items_.push_back(std::move(v));
        ws();
        if (p_ < end_ && *p_ == ',') { ++p_; continue; }
        if (p_ < end_ && *p_ == ']') { ++p_; return true; }
        return false;
      }
    }
    if (c == '"') { out.type_ = Json::String; return string(out.str_); }
    if (literal("true")) { out.type_ = Json::Bool; out.bool_ = true; return true; }
    if (literal("false")) { out.type_ = Json::Bool; out.bool_ = false; return true; }
    if (literal("null")) { out.type_ = Json::Null; return true; }
    if (c == '-' || (c >= '0' && c <= '9')) {
      std::string num;
      while (p_ < end_ && strchr("+-0123456789.eE", *p_)) num += *p_++;
      out.type_ = Json::Number;
      out.num_ = strtod(num.c_str(), nullptr);
      return true;
    }
    return false;
  }
};

Json Json::parse(const std::string& text, bool* ok) {
  Json out;
  bool good = JsonParser(text).parseDocument(out);
  if (!good) out = Json();
  if (ok) *ok = good;
  return out;
}

std::string jsonEscape(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else out += (char)c;
    }
  }
  return out + "\"";
}
