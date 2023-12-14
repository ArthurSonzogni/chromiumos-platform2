// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/utils/mojo_output.h"

#include <string>

#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

}  // namespace

std::string ToStr(mojom::ServiceName name) {
  switch (name) {
    case mojom::ServiceName::kKiosk:
      return "kKiosk";
    case mojom::ServiceName::kUnmappedEnumField:
      return "kUnmappedEnumField";
  }
}

std::string ToStr(mojom::ActionType action) {
  switch (action) {
    case mojom::ActionType::kNoOperation:
      return "kNoOperation";
    case mojom::ActionType::kNormalReboot:
      return "kNormalReboot";
    case mojom::ActionType::kForceReboot:
      return "kForceReboot";
    case mojom::ActionType::kUnmappedEnumField:
      return "kUnmappedEnumField";
  }
}

}  // namespace heartd
