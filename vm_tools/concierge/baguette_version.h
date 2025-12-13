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
constexpr char kBaguetteVersion[] = "2025-12-13-000110_e2057a6b3640e1babc9b1057d7f07ab20ed55844";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b518afd3e750e501ef7c547b75d24141972b6a27b34726a6a1e73c2a64c8d932";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b05315384554dbfe93271cc49f63f04a5e6fb34a2029ab86e1058fd2097d08c1";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
