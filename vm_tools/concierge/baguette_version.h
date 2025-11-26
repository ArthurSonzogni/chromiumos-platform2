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
constexpr char kBaguetteVersion[] = "2025-11-26-000059_fa50ea05d7d30d76e9220834e48080b615b64472";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "778e5f550f5260fd02f26762f6a018c1c035fe7443fdd1e56e07a381c76f8fb5";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "32f9abf5b578b80e8c95e5c166acc118e95c560e96c2bb8db01a3de0157893bf";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
