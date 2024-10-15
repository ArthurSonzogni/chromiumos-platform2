// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mojom/token_mojom_traits.h"

#include <base/token.h>

namespace mojo {

bool StructTraits<mojo_base::mojom::TokenDataView, base::Token>::Read(
    mojo_base::mojom::TokenDataView data, base::Token* out) {
  *out = base::Token{data.high(), data.low()};
  return true;
}

}  // namespace mojo
