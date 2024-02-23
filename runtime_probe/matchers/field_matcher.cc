// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/matchers/field_matcher.h"

#include <optional>
#include <string>
#include <string_view>

#include <base/strings/string_util.h>

namespace runtime_probe::internal {
namespace {

std::string_view RemoveLeadingZero(std::string_view in) {
  for (int i = 0; i < in.size(); i++) {
    if (in[i] != '0') {
      return in.substr(i);
    }
  }
  return std::string_view{};
}

}  // namespace

std::optional<std::string> ParseAndFormatIntegerString(const std::string& in) {
  std::string_view trimmed =
      TrimWhitespaceASCII(in, base::TrimPositions::TRIM_ALL);
  std::string res;
  if (!trimmed.empty() && trimmed[0] == '-') {
    res = "-";
    trimmed = trimmed.substr(1);
  }
  if (trimmed.empty()) {
    return std::nullopt;
  }
  trimmed = RemoveLeadingZero(trimmed);
  if (trimmed.empty()) {
    return "0";
  }
  for (char c : trimmed) {
    if (!base::IsAsciiDigit(c)) {
      return std::nullopt;
    }
  }
  return res + std::string{trimmed};
}

std::optional<std::string> ParseAndFormatHexString(const std::string& in) {
  std::string_view trimmed =
      TrimWhitespaceASCII(in, base::TrimPositions::TRIM_ALL);
  if (base::StartsWith(trimmed, "0x", base::CompareCase::INSENSITIVE_ASCII)) {
    trimmed = trimmed.substr(2);
  }
  if (trimmed.empty()) {
    return std::nullopt;
  }
  trimmed = RemoveLeadingZero(trimmed);
  if (trimmed.empty()) {
    return "0";
  }
  for (char c : trimmed) {
    if (!base::IsHexDigit(c)) {
      return std::nullopt;
    }
  }
  return base::ToLowerASCII(trimmed);
}

}  // namespace runtime_probe::internal
