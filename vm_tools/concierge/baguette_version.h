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
constexpr char kBaguetteVersion[] = "2025-08-12-000111_8201e7a760f3b5f96636c7b480cb1fbcb7c735cb";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2155b97b5b0f95a20451804880a89080f33afaf4e8dc1c5adde400058e25f346";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "76ccf325f3a93030ac06dc8168bddf3fcdeb15fafded14c738b974195720b0df";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
