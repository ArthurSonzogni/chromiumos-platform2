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
constexpr char kBaguetteVersion[] = "2025-04-16-000104_abefc2a3ef601f9368ebb6fc5169384e57aff5e0";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "30535991206057ab0cd93fe124bc4bbe83eb83388371e4022e0989fbc15979e5";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e899ef018b958bbe44eb14dc9fb0e670b5411e018e17a16a1e68bb6bb8a4aa2b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
