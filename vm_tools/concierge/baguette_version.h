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
constexpr char kBaguetteVersion[] = "2025-11-17-000119_1b23a078901d9db4a669971e16d7f699f5342cc7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1a64c561b637180778dd18f3ee253b17f629a1874d28246e8ef6b059efaaa6e8";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6b1f4357064c1c8a64ae62af41a02459a7d68be3a26c2070154870b31ead4501";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
