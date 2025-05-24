// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2025-05-24-000105_ea799111e68309dc74693ba6950ac341c7c7262c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "139a574c6fdf15ca0881d36f6692a9130109619bf548e61c66640a065261d832";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7cf04aa08c90b62d227a59528169a9913014fb2d2336f0f3ebf68d168bb9d8c1";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
