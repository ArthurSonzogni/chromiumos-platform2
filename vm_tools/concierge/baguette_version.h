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
constexpr char kBaguetteVersion[] = "2025-10-20-000114_49cf22212a248f40d9c0f1e3c8ec1fa7617e3604";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f20c6fda6ec4676531a89a7b73a4dcce310319911bb43d0a11dff53f37b3472d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "43f449a01e5a1d05929e8717b21be1fed55729ec619504f4a3aa318e54842463";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
