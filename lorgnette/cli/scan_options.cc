// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/cli/scan_options.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "lorgnette/cli/commands.h"

namespace {

std::optional<bool> ParseBoolVal(const std::string& value) {
  if (value == "1" || value == "true" || value == "yes") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no") {
    return false;
  }
  return std::nullopt;
}

std::optional<std::vector<int>> ParseIntVal(const std::string& value) {
  std::vector<int> result;

  for (const auto& s : base::SplitString(value, ",", base::TRIM_WHITESPACE,
                                         base::SPLIT_WANT_NONEMPTY)) {
    char* end;
    int i = strtol(s.c_str(), &end, 10);
    if (!end || *end != '\0') {
      return std::nullopt;
    }
    result.push_back(i);
  }

  return result;
}

std::optional<std::vector<double>> ParseFloatVal(const std::string& value) {
  std::vector<double> result;

  for (const auto& s : base::SplitString(value, ",", base::TRIM_WHITESPACE,
                                         base::SPLIT_WANT_NONEMPTY)) {
    char* end;
    double d = strtod(s.c_str(), &end);
    if (!end || *end != '\0') {
      return std::nullopt;
    }
    result.push_back(d);
  }

  return result;
}

}  // namespace

namespace lorgnette::cli {

base::StringPairs GetScanOptions(const std::vector<std::string>& args) {
  base::StringPairs options;

  bool parse_options = false;
  for (const std::string& arg : args) {
    if (base::Contains(kCommandMap, arg) &&
        kCommandMap.at(arg) == Command::kSetOptions) {
      parse_options = true;
      continue;
    }
    if (!parse_options) {
      continue;
    }

    size_t eq = arg.find("=", 1);
    if (eq == std::string::npos) {
      // Stop parsing at the first argument that isn't a valid option setting.
      break;
    }

    if (base::StartsWith(arg, "-")) {
      // Count -arg as the end of settings as well.
      break;
    }

    options.emplace_back(
        base::TrimWhitespaceASCII(arg.substr(0, eq), base::TRIM_ALL),
        base::TrimWhitespaceASCII(arg.substr(eq + 1), base::TRIM_ALL));
  }

  return options;
}

std::optional<lorgnette::SetOptionsRequest> MakeSetOptionsRequest(
    const lorgnette::ScannerConfig& config, const base::StringPairs& options) {
  lorgnette::SetOptionsRequest request;

  for (const auto& [option, raw_value] : options) {
    if (!config.options().contains(option)) {
      std::cerr << "Option " << option << " not found" << std::endl;
      return std::nullopt;
    }

    lorgnette::ScannerOption* setting = request.add_options();
    setting->set_name(option);
    switch (config.options().at(option).option_type()) {
      case lorgnette::TYPE_BOOL: {
        std::optional<bool> value = ParseBoolVal(raw_value);
        if (!value.has_value()) {
          std::cerr << "Unable to parse \"" << raw_value << "\" as boolean for "
                    << option << std::endl;
          return std::nullopt;
        }
        setting->set_option_type(lorgnette::TYPE_BOOL);
        setting->set_bool_value(value.value());
        break;
      }
      case lorgnette::TYPE_INT: {
        std::optional<std::vector<int>> value = ParseIntVal(raw_value);
        if (!value.has_value()) {
          std::cerr << "Unable to parse \"" << raw_value
                    << "\" as int list for " << option << std::endl;
          return std::nullopt;
        }
        setting->set_option_type(lorgnette::TYPE_INT);
        for (int i : value.value()) {
          setting->mutable_int_value()->add_value(i);
        }
        break;
      }
      case lorgnette::TYPE_FIXED: {
        std::optional<std::vector<double>> value = ParseFloatVal(raw_value);
        if (!value.has_value()) {
          std::cerr << "Unable to parse \"" << raw_value
                    << "\" as float list for " << option << std::endl;
          return std::nullopt;
        }
        setting->set_option_type(lorgnette::TYPE_FIXED);
        for (double d : value.value()) {
          setting->mutable_fixed_value()->add_value(d);
        }
        break;
      }
      case lorgnette::TYPE_STRING:
        setting->set_option_type(lorgnette::TYPE_STRING);
        setting->set_string_value(raw_value);
        break;
      default:
        std::cerr << "Option " << option << " cannot take a value" << std::endl;
        return std::nullopt;
    }
  }

  *request.mutable_scanner() = config.scanner();
  return request;
}

}  // namespace lorgnette::cli
