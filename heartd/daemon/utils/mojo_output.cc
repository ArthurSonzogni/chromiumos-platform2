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
      return "Kiosk";
    case mojom::ServiceName::kUnmappedEnumField:
      return "Unmapped Enum Field";
  }
}

std::string ToStr(mojom::ActionType action) {
  switch (action) {
    case mojom::ActionType::kNoOperation:
      return "No Operation";
    case mojom::ActionType::kNormalReboot:
      return "Normal Reboot";
    case mojom::ActionType::kForceReboot:
      return "Forced Reboot";
    case mojom::ActionType::kSyncData:
      return "Sync Data";
    case mojom::ActionType::kUnmappedEnumField:
      return "Unmapped Enum Field";
  }
}

}  // namespace heartd
