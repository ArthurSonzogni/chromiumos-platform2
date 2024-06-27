// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJOM_READ_ONLY_FILE_MOJOM_TRAITS_H_
#define MOJOM_READ_ONLY_FILE_MOJOM_TRAITS_H_

#include <base/files/file.h>

#include "odml/mojom/read_only_file.mojom.h"

namespace mojo {

template <>
struct StructTraits<mojo_base::mojom::ReadOnlyFileDataView, base::File> {
  static bool IsNull(const base::File& file) { return !file.IsValid(); }

  static void SetToNull(base::File* file) { *file = base::File(); }

  static mojo::PlatformHandle fd(base::File& file);
  static bool async(base::File& file) { return file.async(); }
  static bool Read(mojo_base::mojom::ReadOnlyFileDataView data,
                   base::File* file);
};

}  // namespace mojo

#endif  // MOJOM_READ_ONLY_FILE_MOJOM_TRAITS_H_
