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
constexpr char kBaguetteVersion[] = "2026-11-25-000117_e081121adb98ffe52a3ea84a81d78d728f1e66be";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9e95193b1d70391e4c60f5dfd5a023c2ad5ea221bed6149a8d16a04270c95f63";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "21daf56ca3ac207e57fe1fe616fa46705fccc8f20ea396113fd758d14274b06c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
