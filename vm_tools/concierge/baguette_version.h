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
constexpr char kBaguetteVersion[] = "2025-08-17-000124_bca4815d5ac3e48a1cf3e0d3f5fa4aa49b9af64b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "70be5250c3e4f283e1ae9c5af95c7ca170d1e14111f3fe1e3a7edb9fb7eb6d99";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6739b3c1f074050b7b9ada31ab88efbe1e9a625ffcd418591270ca1a396dd17a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
