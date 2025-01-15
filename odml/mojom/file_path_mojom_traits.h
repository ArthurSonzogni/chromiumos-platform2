// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJOM_FILE_PATH_MOJOM_TRAITS_H_
#define MOJOM_FILE_PATH_MOJOM_TRAITS_H_

#include <base/component_export.h>
#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <mojo/public/cpp/bindings/struct_traits.h>

#include "ml/mojom/file_path.mojom.h"

namespace mojo {

template <>
struct StructTraits<mojo_base::mojom::FilePathDataView, base::FilePath> {
  static const base::FilePath::StringType& path(const base::FilePath& path) {
    return path.value();
  }

  static bool Read(mojo_base::mojom::FilePathDataView data,
                   base::FilePath* out);
};

template <>
struct StructTraits<mojo_base::mojom::RelativeFilePathDataView,
                    base::FilePath> {
  static const base::FilePath::StringType& path(const base::FilePath& path);

  static bool Read(mojo_base::mojom::RelativeFilePathDataView data,
                   base::FilePath* out);
};

}  // namespace mojo

#endif  // MOJOM_FILE_PATH_MOJOM_TRAITS_H_
