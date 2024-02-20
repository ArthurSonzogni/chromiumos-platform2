// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/matchers/logical_matcher.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/values.h>

#include "runtime_probe/matchers/matcher.h"

namespace runtime_probe {
namespace {

std::vector<std::unique_ptr<Matcher>> ParseMatchers(
    const base::Value::List& operands) {
  if (operands.empty()) {
    LOG(ERROR)
        << "Logical matcher must have at least one sub-matcher in operand";
    return {};
  }
  std::vector<std::unique_ptr<Matcher>> matchers;
  for (const auto& value : operands) {
    if (!value.is_dict()) {
      LOG(ERROR) << "Logical matcher takes dict operands, but got " << value;
      return {};
    }
    auto matcher = Matcher::FromValue(value.GetDict());
    if (!matcher) {
      LOG(ERROR) << "Failed to parse matcher from " << value;
      return {};
    }
    matchers.push_back(std::move(matcher));
  }
  return matchers;
}

}  // namespace

// static
std::unique_ptr<AndMatcher> AndMatcher::Create(
    const base::Value::List& operands) {
  auto matchers = ParseMatchers(operands);
  if (matchers.empty()) {
    return nullptr;
  }
  return std::unique_ptr<AndMatcher>(new AndMatcher(std::move(matchers)));
}

AndMatcher::AndMatcher(std::vector<std::unique_ptr<Matcher>> matchers)
    : matchers_(std::move(matchers)) {
  CHECK(!matchers_.empty());
}

AndMatcher::~AndMatcher() = default;

bool AndMatcher::Match(const base::Value::Dict& component) const {
  for (const auto& matcher : matchers_) {
    if (!matcher->Match(component)) {
      return false;
    }
  }
  return true;
}

// static
std::unique_ptr<OrMatcher> OrMatcher::Create(
    const base::Value::List& operands) {
  auto matchers = ParseMatchers(operands);
  if (matchers.empty()) {
    return nullptr;
  }
  return std::unique_ptr<OrMatcher>(new OrMatcher(std::move(matchers)));
}

OrMatcher::OrMatcher(std::vector<std::unique_ptr<Matcher>> matchers)
    : matchers_(std::move(matchers)) {
  CHECK(!matchers_.empty());
}

OrMatcher::~OrMatcher() = default;

bool OrMatcher::Match(const base::Value::Dict& component) const {
  for (const auto& matcher : matchers_) {
    if (matcher->Match(component)) {
      return true;
    }
  }
  return false;
}

}  // namespace runtime_probe
