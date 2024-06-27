// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BASE_UUID_MOJOM_TRAITS_H_
#define MOJO_PUBLIC_CPP_BASE_UUID_MOJOM_TRAITS_H_

#include "base/uuid.h"
#include "odml/mojom//uuid.mojom-shared.h"

namespace mojo {

template <>
struct StructTraits<mojo_base::mojom::UuidDataView, base::Uuid> {
  static const std::string& value(const base::Uuid& uuid) {
    return uuid.AsLowercaseString();
  }

  static bool Read(mojo_base::mojom::UuidDataView data, base::Uuid* out);
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BASE_UUID_MOJOM_TRAITS_H_
