
#pragma once
#include <string>
#include <vector>

class Json {
 public:
  enum Type { Null, Bool, Number, String, Array, Object };

  static Json parse(const std::string& text, bool* ok = nullptr);

  Type type() const { return type_; }
  bool isNull() const { return type_ == Null; }
  bool isObject() const { return type_ == Object; }


  const Json& operator[](const char* key) const;
  const Json& operator[](size_t index) const;
  size_t size() const { return items_.size(); }
  const std::vector<std::string>& keys() const { return keys_; }

  std::string str(const std::string& fallback = "") const { return type_ == String ? str_ : fallback; }
  double num(double fallback = 0) const { return type_ == Number ? num_ : fallback; }
  bool boolean(bool fallback = false) const { return type_ == Bool ? bool_ : fallback; }

 private:
  friend class JsonParser;
  Type type_ = Null;
  bool bool_ = false;
  double num_ = 0;
  std::string str_;
  std::vector<std::string> keys_;
  std::vector<Json> items_;
};

std::string jsonEscape(const std::string& s);
