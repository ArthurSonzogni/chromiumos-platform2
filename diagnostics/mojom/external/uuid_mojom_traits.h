// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_MOJOM_EXTERNAL_UUID_MOJOM_TRAITS_H_
#define DIAGNOSTICS_MOJOM_EXTERNAL_UUID_MOJOM_TRAITS_H_

#include <string>

#include <base/uuid.h>
#include <mojo/public/cpp/bindings/struct_traits.h>

#include "diagnostics/mojom/external/uuid.mojom.h"

namespace mojo {

template <>
struct StructTraits<ash::cros_healthd::external::mojo_base::mojom::UuidDataView,
                    base::Uuid> {
  static std::string value(const base::Uuid& uuid) {
    return uuid.AsLowercaseString();
  }

  static bool Read(
      ash::cros_healthd::external::mojo_base::mojom::UuidDataView data,
      base::Uuid* uuid) {
    std::string result;
    if (!data.ReadValue(&result))
      return false;
    *uuid = base::Uuid::ParseCaseInsensitive(result);
    return uuid->is_valid();
  }
};

}  // namespace mojo

#endif  // DIAGNOSTICS_MOJOM_EXTERNAL_UUID_MOJOM_TRAITS_H_
