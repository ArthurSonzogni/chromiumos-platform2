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
constexpr char kBaguetteVersion[] = "2025-07-12-000123_be14be1a9ff6723d0c88200914a04897fefff7c2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9ddddb81e4d25037422ff64a569a0add7462139db40e5a1ed629021bc8ce4867";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "af0d5ca2276aabcf3981d8baee13630ac6d5b7a6f12deabae4f5bdd6bc4decb5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
