// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/variant_utils.h"

#include <re2/re2.h>

namespace {

const char kOptionRegexMismatchErrorString[] =
    "org.chromium.debugd.error.OptionRegexMismatch";
}

namespace debugd {

bool AddIntOption(SandboxedProcess* process,
                  const brillo::VariantDictionary& options,
                  const std::string& key,
                  const std::string& flag_name,
                  brillo::ErrorPtr* error) {
  int value;
  ParseResult result = GetOption(options, key, &value, error);
  if (result == ParseResult::PARSED)
    process->AddIntOption(flag_name, value);

  return result != ParseResult::PARSE_ERROR;
}

bool AddBoolOption(SandboxedProcess* process,
                   const brillo::VariantDictionary& options,
                   const std::string& key,
                   const std::string& flag_name,
                   brillo::ErrorPtr* error) {
  int value;
  ParseResult result = GetOption(options, key, &value, error);
  if (result == ParseResult::PARSED && value)
    process->AddArg(flag_name);

  return result != ParseResult::PARSE_ERROR;
}

bool AddStringOption(SandboxedProcess* process,
                     const brillo::VariantDictionary& options,
                     const std::string& key,
                     const std::string& flag_name,
                     const std::string& value_re,
                     brillo::ErrorPtr* error) {
  std::string value;
  ParseResult result = GetOption(options, key, &value, error);
  if (result != ParseResult::PARSED || value.empty())
    return result != ParseResult::PARSE_ERROR;

  if (!value_re.empty() && RE2::FullMatch(value, value_re)) {
    process->AddStringOption(flag_name, value);
    return true;
  }

  DEBUGD_ADD_ERROR_FMT(error, kOptionRegexMismatchErrorString,
                       "<string option (%s) failed regex match>", key.c_str());
  return false;
}

}  // namespace debugd
