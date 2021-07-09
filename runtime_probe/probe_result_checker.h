// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_PROBE_RESULT_CHECKER_H_
#define RUNTIME_PROBE_PROBE_RESULT_CHECKER_H_

#include "runtime_probe/field_converter.h"

#include <map>
#include <memory>
#include <string>

#include <base/values.h>
#include <gtest/gtest.h>

namespace runtime_probe {

// Holds |expect| attribute of a |ProbeStatement|.
//
// |expect| attribute should be a |Value| with following format:
// {
//   <key_of_probe_result>: [<required:bool>, <expected_type:string>,
//                           <optional_validate_rule:string>]
// }
//
// Currently, we support the following expected types:
// - "int"  (use |IntegerFieldConverter|)
// - "hex"  (use |HexFieldConverter|)
// - "double"  (use |DoubleFieldConverter|)
// - "str"  (use |StringFieldConverter|)
//
// |ProbeResultChecker| will first try to convert each field to |expected_type|.
// Then, if |optional_validate_rule| is given, will check if converted value
// match the rule.
//
// TODO(b/121354690): Handle |optional_validate_rule|.
class ProbeResultChecker {
 public:
  static std::unique_ptr<ProbeResultChecker> FromValue(
      const base::Value& dict_value);

  // Apply |expect| rules to |probe_result|
  //
  // @return |true| if all required fields are converted successfully.
  bool Apply(base::Value* probe_result) const;

 private:
  std::map<std::string, std::unique_ptr<FieldConverter>> required_fields_;
  std::map<std::string, std::unique_ptr<FieldConverter>> optional_fields_;

  FRIEND_TEST(ProbeResultCheckerTest, TestFromValue);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_RESULT_CHECKER_H_
