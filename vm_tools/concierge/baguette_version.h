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
constexpr char kBaguetteVersion[] = "2026-09-12-000114_9ce3dce62a41dfca29bbaba690e6d9229230eaa1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "79a1e3d6e09fd3b1354fd4b6b01e6f39544998c2b7ee00025e21df311e1b5881";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7b90f821120655917721b4a077b2c86ccd47b62f3dc476f31f949bb4f16577eb";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
