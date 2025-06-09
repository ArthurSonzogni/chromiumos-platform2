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
constexpr char kBaguetteVersion[] = "2025-06-09-000143_9f25169e5bf1eadd2cfa08d8dc659f8a4632c040";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "757a1674ca0bb8017df354804cd0d5bf8aae937b05818ab5b0fa6a0fccebd775";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3399ca97afae1f23f963e6081f258b3e6ee26ff3bc114f80b8c921a94b02bdd3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
