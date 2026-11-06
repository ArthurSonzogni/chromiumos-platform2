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
constexpr char kBaguetteVersion[] = "2026-11-06-000512_289e996f7e5c5168886426c5241b4839eef3da4f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "65c1c00f9214450c3f3e405573157d4e3fc5de85220a8986045645bf9fff016d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "659fb364f3227c1a0e1c6e9e3603439ab2d6379052f187cf5d80a750a8967e58";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
