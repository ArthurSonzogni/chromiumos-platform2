// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJOM_FILE_MOJOM_TRAITS_H_
#define MOJOM_FILE_MOJOM_TRAITS_H_

#include <base/files/file.h>
#include <mojo/public/cpp/bindings/struct_traits.h>

#include "odml/mojom/file.mojom.h"

namespace mojo {

template <>
struct StructTraits<mojo_base::mojom::FileDataView, base::File> {
  static bool IsNull(const base::File& file) { return !file.IsValid(); }

  static void SetToNull(base::File* file) { *file = base::File(); }

  static mojo::PlatformHandle fd(base::File& file);
  static bool async(base::File& file) { return file.async(); }
  static bool Read(mojo_base::mojom::FileDataView data, base::File* file);
};

}  // namespace mojo

#endif  // MOJOM_FILE_MOJOM_TRAITS_H_
