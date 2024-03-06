// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_MATCHERS_FIELD_MATCHER_H_
#define RUNTIME_PROBE_MATCHERS_FIELD_MATCHER_H_

#include <memory>
#include <optional>
#include <string>

#include <base/logging.h>
#include <base/values.h>

#include "runtime_probe/matchers/matcher.h"

namespace runtime_probe {
namespace internal {

// Parses an integer string and format it to a general format (trimmed, no
// leading 0). Returns nullopt if it is not an integer string.
std::optional<std::string> ParseAndFormatIntegerString(const std::string& in);

// Parses an hex string and format it to a general format (trimmed, no 0x
// prefix, no leading 0, lowercase). Returns nullopt if it is not an hex
// string. We assume that all hex string is unsigned.
std::optional<std::string> ParseAndFormatHexString(const std::string& in);

enum class FieldEqualMatcherType : uint8_t {
  kString,
  kInteger,
  kHex,
};

// Implements a matcher that matches a field in probe result. The field value
// will be converted to a specific type for comparison.
template <FieldEqualMatcherType T>
class FieldEqualMatcher : public Matcher {
 public:
  // Creates the matcher that matches if a field |field_name|'s value is
  // |expected|.
  static std::unique_ptr<FieldEqualMatcher<T>> Create(
      const base::Value::List& operands) {
    if (operands.size() != 2 || !operands[0].is_string() ||
        !operands[1].is_string()) {
      LOG(ERROR) << "FieldEqualMatcher takes 2 string operands, but got "
                 << operands;
      return nullptr;
    }
    std::string field_name = operands[0].GetString();
    std::string expected = operands[1].GetString();

    auto expected_parsed = FieldValue::FromString(expected);
    if (!expected_parsed) {
      LOG(ERROR) << "Failed to parse expected value: " << expected;
      return nullptr;
    }
    return std::unique_ptr<FieldEqualMatcher<T>>(
        new FieldEqualMatcher<T>(field_name, *expected_parsed));
  }

  FieldEqualMatcher(const FieldEqualMatcher&) = delete;
  FieldEqualMatcher& operator=(const FieldEqualMatcher&) = delete;
  ~FieldEqualMatcher() override = default;

  // Matcher overrides.
  bool Match(const base::Value::Dict& component) const override {
    const std::string* field_raw_value = component.FindString(field_name_);
    if (!field_raw_value) {
      // Fields not exist never match.
      return false;
    }
    auto field_value_parsed = FieldValue::FromString(*field_raw_value);
    return field_value_parsed && field_value_parsed->Equal(expected_);
  }

 private:
  // Holds the value to be comparison.
  class FieldValue {
   public:
    // Creates from a string.
    static std::optional<FieldValue> FromString(const std::string& value) {
      std::optional<std::string> parsed_value;
      if constexpr (T == FieldEqualMatcherType::kString) {
        parsed_value = value;
      } else if constexpr (T == FieldEqualMatcherType::kInteger) {
        parsed_value = ParseAndFormatIntegerString(value);
      } else if constexpr (T == FieldEqualMatcherType::kHex) {
        parsed_value = ParseAndFormatHexString(value);
      } else {
        static_assert(false, "Unsupported field type");
      }

      if (parsed_value) {
        return FieldValue{*parsed_value};
      }
      return std::nullopt;
    }

    FieldValue(const FieldValue&) = default;
    FieldValue& operator=(const FieldValue&) = default;

    // Checks if this equals to |oth|.
    bool Equal(const FieldValue& oth) const { return value_ == oth.value_; }

   private:
    explicit FieldValue(const std::string& value) : value_(value) {}

    std::string value_;
  };

  FieldEqualMatcher(const std::string& field_name, const FieldValue& expected)
      : field_name_(field_name), expected_(expected) {}

  std::string field_name_;
  FieldValue expected_;
};

}  // namespace internal

using StringEqualMatcher =
    internal::FieldEqualMatcher<internal::FieldEqualMatcherType::kString>;
using IntegerEqualMatcher =
    internal::FieldEqualMatcher<internal::FieldEqualMatcherType::kInteger>;
using HexEqualMatcher =
    internal::FieldEqualMatcher<internal::FieldEqualMatcherType::kHex>;

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_MATCHERS_FIELD_MATCHER_H_
