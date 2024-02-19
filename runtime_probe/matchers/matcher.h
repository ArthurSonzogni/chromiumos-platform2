// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_MATCHERS_MATCHER_H_
#define RUNTIME_PROBE_MATCHERS_MATCHER_H_

#include <memory>
#include <string>

#include <base/values.h>

namespace runtime_probe {

// Holds a |matcher| attribute of a |ProbeStatement| with following JSON schema:
//   {
//     "operator": <operator_name:string>,
//     "operand": [<operands:string>]
//   }
class Matcher {
 public:
  // Creates from a dict value. Returns nullptr if the syntax is not correct.
  static std::unique_ptr<Matcher> FromValue(const base::Value::Dict& value);

  virtual ~Matcher() = default;

  // Matches a component in probe result returned by a probe function. Returns
  // true if the matcher matches.
  virtual bool Match(const base::Value::Dict& component) const = 0;

 protected:
  Matcher() = default;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_MATCHERS_MATCHER_H_
