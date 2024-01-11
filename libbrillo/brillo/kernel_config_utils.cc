// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libbrillo/brillo/kernel_config_utils.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

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

enum class FindType { kValue, kFlag };

struct ValueBound {
  std::string::const_iterator begin;
  std::string::const_iterator end;
};

// Tokenization logic to search config for desired flags or values.
bool Find(const std::string& kernel_config,
          const std::string& target,
          FindType type,
          ValueBound* value_bound) {
  // Setup tokenizer to split up on any white space while also not breaking
  // quoted values.
  base::StringTokenizer tokenizer(kernel_config, base::kWhitespaceASCII);
  tokenizer.set_quote_chars(std::string{kDoubleQuote});
  std::vector<std::string> target_variants;

  switch (type) {
    case FindType::kValue:
      // Token should start with `target=` or `"target"=`.
      target_variants = {
          kDoubleQuote + target + kDoubleQuote + kEquals,
          target.ends_with(kEquals) ? target : (target + kEquals)};
      break;
    case FindType::kFlag:
      // Match `target`, `"target"`, `target=...`, or `"target"=...`
      target_variants = {target, kDoubleQuote + target + kDoubleQuote,
                         target + kEquals,
                         kDoubleQuote + target + kDoubleQuote + kEquals};
      break;
  }

  while (tokenizer.GetNext()) {
    const auto token = tokenizer.token();
    if (token == kTerminator)
      break;

    switch (type) {
      case FindType::kValue:
        for (const auto& target_variant : target_variants) {
          if (!token.starts_with(target_variant))
            continue;

          if (value_bound) {
            *value_bound = {tokenizer.token_begin() + target_variant.size(),
                            tokenizer.token_end()};
          }
          return true;
        }
        break;

      case FindType::kFlag:
        for (const auto& target_variant : target_variants) {
          if (token == target_variant || (target_variant.ends_with(kEquals) &&
                                          token.starts_with(target_variant)))
            return true;
        }
        break;
    }
  }

  return false;
}

// Value find logic that is common for extracting and setting values in the
// kernel config.
bool GetValueOffset(const std::string& kernel_config,
                    const std::string& key,
                    ValueBound* value_bound) {
  return Find(kernel_config, key, FindType::kValue, value_bound);
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
  ValueBound value_bound;
  if (!GetValueOffset(kernel_config, key, &value_bound)) {
    return std::nullopt;
  }
  auto value = std::string{value_bound.begin, value_bound.end};
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
  ValueBound value_bound;
  if (!GetValueOffset(kernel_config, key, &value_bound)) {
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
  kernel_config.replace(value_bound.begin, value_bound.end, adjusted_value);
  return true;
}

bool FlagExists(const std::string& kernel_config, const std::string& flag) {
  return Find(kernel_config, flag, FindType::kFlag, nullptr);
}

}  // namespace brillo
