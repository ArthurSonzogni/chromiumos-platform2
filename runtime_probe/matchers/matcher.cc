// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/matchers/matcher.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/values.h>

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

  // TODO(chungsheng): Implement matchers.
  LOG(ERROR) << "Unsupported matcher operator " << op;
  return nullptr;
}

}  // namespace runtime_probe
