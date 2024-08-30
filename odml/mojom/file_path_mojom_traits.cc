// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mojom/file_path_mojom_traits.h"

namespace mojo {

// static
bool StructTraits<mojo_base::mojom::FilePathDataView, base::FilePath>::Read(
    mojo_base::mojom::FilePathDataView data, base::FilePath* out) {
  base::FilePath::StringPieceType path_view;
  if (!data.ReadPath(&path_view)) {
    return false;
  }
  *out = base::FilePath(path_view);
  return true;
}

// static
const base::FilePath::StringType&
StructTraits<mojo_base::mojom::RelativeFilePathDataView, base::FilePath>::path(
    const base::FilePath& path) {
  CHECK(!path.IsAbsolute());
  CHECK(!path.ReferencesParent());
  return path.value();
}

// static
bool StructTraits<mojo_base::mojom::RelativeFilePathDataView, base::FilePath>::
    Read(mojo_base::mojom::RelativeFilePathDataView data, base::FilePath* out) {
  base::FilePath::StringPieceType path_view;
  if (!data.ReadPath(&path_view)) {
    return false;
  }
  *out = base::FilePath(path_view);

  if (out->IsAbsolute()) {
    return false;
  }
  if (out->ReferencesParent()) {
    return false;
  }
  return true;
}

}  // namespace mojo
