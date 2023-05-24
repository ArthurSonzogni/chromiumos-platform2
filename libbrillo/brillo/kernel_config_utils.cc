// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libbrillo/brillo/kernel_config_utils.h"

#include <algorithm>
#include <optional>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_tokenizer.h>
#include <base/strings/string_util.h>

namespace brillo {

namespace {
constexpr char kKernelCmdline[] = "/proc/cmdline";
constexpr char kDoubleQuote = '"';
constexpr char kEquals = '=';
constexpr char kTerminator[] = "--";

constexpr size_t kMaxKernelConfigSize = 4096;

// Value find logic that is common for extracting and setting values in the
// kernel config.
bool GetValueOffset(const std::string& kernel_config,
                    const std::string& key,
                    std::string::const_iterator& value_begin,
                    std::string::const_iterator& value_end) {
  // Setup tokenizer to split up on any white space while also not breaking
  // quoted values.
  base::StringTokenizer tokenizer(kernel_config, base::kWhitespaceASCII);
  tokenizer.set_quote_chars(std::string{kDoubleQuote});
  while (tokenizer.GetNext()) {
    const auto token = tokenizer.token();
    if (token == kTerminator)
      break;

    // Token should start with `key=`.
    if (!token.starts_with(key + kEquals) || token.size() < key.size() + 1)
      continue;
    // Since key + `=` matched, value starts at key size + 1.
    value_begin = tokenizer.token_begin() + key.size() + 1;
    value_end = tokenizer.token_end();
    return true;
  }
  return false;
}
}  // namespace

std::optional<std::string> GetCurrentKernelConfig() {
  std::string cmdline;
  if (!base::ReadFileToStringWithMaxSize(base::FilePath(kKernelCmdline),
                                         &cmdline, kMaxKernelConfigSize)) {
    PLOG(ERROR) << "Failed to read kernel command line from " << kKernelCmdline;
    return std::nullopt;
  }
  return cmdline;
}

std::optional<std::string> ExtractKernelArgValue(
    const std::string& kernel_config,
    const std::string& key,
    const bool strip_quotes) {
  std::string::const_iterator value_begin, value_end;
  if (!GetValueOffset(kernel_config, key, value_begin, value_end)) {
    return std::nullopt;
  }
  auto value = std::string{value_begin, value_end};
  if (value.starts_with(kDoubleQuote)) {
    if (value.length() == 1 || !value.ends_with(kDoubleQuote)) {
      // Value is corrupt if there's no end quote.
      return std::nullopt;
    }
    if (strip_quotes) {
      value = value.substr(1, value.length() - 2);
    }
  }
  return value;
}

bool SetKernelArg(const std::string& key,
                  const std::string& value,
                  std::string& kernel_config) {
  std::string::const_iterator value_begin, value_end;
  if (!GetValueOffset(kernel_config, key, value_begin, value_end)) {
    return false;
  }
  std::string adjusted_value = value;

  // Check for white space and quoted values.
  const bool value_has_white_space =
      std::any_of(value.begin(), value.end(),
                  [](const char& c) { return base::IsAsciiWhitespace(c); });
  const bool quoted_value = value.size() > 1 &&
                            value.starts_with(kDoubleQuote) &&
                            value.ends_with(kDoubleQuote);

  // If the new value has spaces, quote it before inserting. Skip quoting if the
  // value is already quoted.
  if (value_has_white_space && !quoted_value) {
    adjusted_value = kDoubleQuote + value + kDoubleQuote;
  }
  kernel_config.replace(value_begin, value_end, adjusted_value);
  return true;
}

}  // namespace brillo
