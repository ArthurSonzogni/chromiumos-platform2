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
constexpr char kBaguetteVersion[] = "2025-07-26-000122_3bbed07c255e491328ef759fc4474a1e29f7be95";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "faf2c24fd9ad81f781920e6d2bc5c6b2112e5d040967cd015204fb7f16a597e7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a6bb0105ae0126d07ddaed88eeb14b89ea5d6db28e694d5a234f9115c24df11b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
