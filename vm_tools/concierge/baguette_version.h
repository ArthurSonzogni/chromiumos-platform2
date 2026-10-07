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
constexpr char kBaguetteVersion[] = "2026-10-07-000113_25eef3c185f0e5f7b2070c29bbf2869ec5e9b2fd";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "46b4ad4b2282a5fdb2de4bcb371df0b761a9ce0b3afb92b919c90bcde180d88a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "90f4abbd96a1722e880b39d744219c1c946127e5774648adee2d119820bfe453";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
