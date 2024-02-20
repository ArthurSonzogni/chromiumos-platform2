// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_MATCHERS_LOGICAL_MATCHER_H_
#define RUNTIME_PROBE_MATCHERS_LOGICAL_MATCHER_H_

#include <memory>
#include <vector>

#include <base/values.h>

#include "runtime_probe/matchers/matcher.h"

namespace runtime_probe {

// Implements a matcher that matches if all sub-matchers match.
class AndMatcher : public Matcher {
 public:
  static std::unique_ptr<AndMatcher> Create(const base::Value::List& operands);

  AndMatcher(const AndMatcher&) = delete;
  AndMatcher& operator=(const AndMatcher&) = delete;
  ~AndMatcher() override;

  // Matcher overrides.
  bool Match(const base::Value::Dict& component) const override;

 private:
  explicit AndMatcher(std::vector<std::unique_ptr<Matcher>> matchers);

  std::vector<std::unique_ptr<Matcher>> matchers_;
};

// Implements a matcher that matches if any sub-matchers match.
class OrMatcher : public Matcher {
 public:
  static std::unique_ptr<OrMatcher> Create(const base::Value::List& operands);

  OrMatcher(const OrMatcher&) = delete;
  OrMatcher& operator=(const OrMatcher&) = delete;
  ~OrMatcher() override;

  // Matcher overrides.
  bool Match(const base::Value::Dict& component) const override;

 private:
  explicit OrMatcher(std::vector<std::unique_ptr<Matcher>> matchers);

  std::vector<std::unique_ptr<Matcher>> matchers_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_MATCHERS_LOGICAL_MATCHER_H_
