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
constexpr char kBaguetteVersion[] = "2025-04-20-000124_69bd635d7cfe0d9e1aa9cf8b1da9856f855dfed5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c3488e953af10541bb3f6935d32787fe46d55fa22a60ca1bf7db6dae02903a54";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "520615c6b84b7c4da4972a70f8a2eb8d776761c692fee7052b3b8055512924e7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
