// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJOM_TOKEN_MOJOM_TRAITS_H_
#define MOJOM_TOKEN_MOJOM_TRAITS_H_

#include <base/token.h>
#include <mojo/public/cpp/bindings/struct_traits.h>

#include "odml/mojom/token.mojom.h"

namespace mojo {

template <>
struct StructTraits<mojo_base::mojom::TokenDataView, base::Token> {
  static uint64_t high(const base::Token& token) { return token.high(); }
  static uint64_t low(const base::Token& token) { return token.low(); }
  static bool Read(mojo_base::mojom::TokenDataView data, base::Token* out);
};

}  // namespace mojo

#endif  // MOJOM_TOKEN_MOJOM_TRAITS_H_
