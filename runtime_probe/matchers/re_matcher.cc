// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/matchers/re_matcher.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/values.h>
#include <pcrecpp.h>

namespace runtime_probe {

// static
std::unique_ptr<ReMatcher> ReMatcher::Create(
    const base::Value::List& operands) {
  if (operands.size() != 2 || !operands[0].is_string() ||
      !operands[1].is_string()) {
    LOG(ERROR) << "ReMatcher takes 2 string operands, but got " << operands;
    return nullptr;
  }
  std::string field_name = operands[0].GetString();
  std::string regex_string = operands[1].GetString();

  auto res =
      std::unique_ptr<ReMatcher>(new ReMatcher(field_name, regex_string));
  if (!res->regex_.error().empty()) {
    LOG(ERROR) << "Failed to parse regex " << regex_string << ": "
               << res->regex_.error();
    return nullptr;
  }
  return res;
}

ReMatcher::ReMatcher(const std::string& field_name,
                     const std::string& regex_string)
    : field_name_(field_name), regex_(regex_string) {}

ReMatcher::~ReMatcher() = default;

bool ReMatcher::Match(const base::Value::Dict& component) const {
  const std::string* field_value = component.FindString(field_name_);
  // Fields not exist never match.
  return field_value && regex_.FullMatch(*field_value);
}

}  // namespace runtime_probe
