// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/matchers/matcher.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/values.h>

#include "runtime_probe/matchers/field_matcher.h"

namespace runtime_probe {

// static
std::unique_ptr<Matcher> Matcher::FromValue(const base::Value::Dict& value) {
  // TODO(chungsheng): Consider using some protobuf type for probe config, so we
  // can use proto to define "operator".
  const std::string* operator_ptr = value.FindString("operator");
  if (!operator_ptr) {
    LOG(ERROR) << "Matcher must have \"operator\" field";
    return nullptr;
  }
  const std::string& op = *operator_ptr;

  const base::Value::List* operands = value.FindList("operand");
  if (!operands) {
    LOG(ERROR) << "Matcher must have \"operand\" field";
    return nullptr;
  }

  if (op == "STRING_EQUAL") {
    if (operands->size() != 2 || !(*operands)[0].is_string() ||
        !(*operands)[1].is_string()) {
      LOG(ERROR) << "Matcher " << op << " takes 2 string operands, but got "
                 << operands;
      return nullptr;
    }
    std::string field_name = (*operands)[0].GetString();
    std::string expected = (*operands)[1].GetString();

    return StringEqualMatcher::Create(field_name, expected);
  }
  LOG(ERROR) << "Unsupported matcher operator " << op;
  return nullptr;
}

}  // namespace runtime_probe
