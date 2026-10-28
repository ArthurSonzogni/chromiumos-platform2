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
constexpr char kBaguetteVersion[] = "2026-10-28-000258_04bee29b721ea50aa0b4188a625e0cee615e32f5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "84933b67b2dcf18a9ebc0002397cc0c2912f43a133ffdceb60521ed561313df7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6e31539438185742d381a4dbcce6ea65963a56b789b055e5485e48ef3dce0873";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
