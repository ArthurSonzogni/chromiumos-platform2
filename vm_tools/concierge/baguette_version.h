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
constexpr char kBaguetteVersion[] = "2025-09-01-000141_e2901eeea25603c5d689646ebbf3abedca21de38";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "197dd4b2283881e4b77daaa9deaf21e57de5cceadff226395f2773961e28e401";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3b239beb4fcd33c5617ff4e2000b78f8170087c3c611e13e49de202cce128b75";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
