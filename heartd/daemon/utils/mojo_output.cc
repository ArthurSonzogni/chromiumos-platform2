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

}  // namespace heartd
