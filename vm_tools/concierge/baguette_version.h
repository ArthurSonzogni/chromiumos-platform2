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
constexpr char kBaguetteVersion[] = "2025-07-01-000120_95ced1f4e254ce07e5ab7ea264e3121e5d61ed2f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4b1c3ea6985fab99616de0609c514b34f4d7ddf4a68c1362568340f97dee7d4f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8e310d758b283f35b30d6383fac9e3b3ff7f3ba83178dd2cf4dc279c1a773eb0";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
