// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_MATCHERS_RE_MATCHER_H_
#define RUNTIME_PROBE_MATCHERS_RE_MATCHER_H_

#include <memory>
#include <string>

#include <base/values.h>
#include <pcrecpp.h>

#include "runtime_probe/matchers/matcher.h"

namespace runtime_probe {

// Implements a matcher that matches a field by regular expression.
class ReMatcher : public Matcher {
 public:
  static std::unique_ptr<ReMatcher> Create(const base::Value::List& operands);
  ReMatcher(const ReMatcher&) = delete;
  ReMatcher& operator=(const ReMatcher&) = delete;
  ~ReMatcher() override;

  // Matcher overrides.
  bool Match(const base::Value::Dict& component) const override;

 private:
  ReMatcher(const std::string& field_name, const std::string& regex_string);

  std::string field_name_;
  pcrecpp::RE regex_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_MATCHERS_RE_MATCHER_H_
