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
constexpr char kBaguetteVersion[] = "2026-09-23-000110_d61273bdb6b7a8d3ec00c7bde30247575633b44d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d80cabba5f854c204e03f0734e004423069f98f606fe981e9a5889a3c9be1fde";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b1eae611856d0806a3ec84bc80f97d5c1e15f7ccc1ed26ccdc2de95b11e52156";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
