// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/middleware/function_name.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <base/no_destructor.h>
#include <base/strings/string_split.h>
#include <brillo/type_name_undecorate.h>
#include <re2/re2.h>

namespace {
constexpr const char kFuncWrapMatchRule[] =
    R"(hwsec::FuncWrapper<&\(*((\(anonymous namespace\)|[\w:])*)[()<>])";

const re2::RE2& GetFuncWrapperRE() {
  static const base::NoDestructor<re2::RE2> rule(kFuncWrapMatchRule);
  return *rule;
}

}  // namespace

namespace hwsec {

std::string ExtractFuncName(const std::string& func_name) {
  std::string result;

  if (!re2::RE2::PartialMatch(func_name, GetFuncWrapperRE(), &result)) {
    return func_name;
  }

  return result;
}

std::string SimplifyFuncName(const std::string& func_name) {
  std::vector<std::string> func_splits = base::SplitString(
      func_name, "::", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_NONEMPTY);

  std::string result;

  for (const std::string& split : func_splits) {
    // Ignore the "hwsec" namespace.
    if (split == "hwsec") {
      continue;
    }

    if (!result.empty()) {
      result += ".";
    }
    result += split;
  }

  return result;
}

}  // namespace hwsec
