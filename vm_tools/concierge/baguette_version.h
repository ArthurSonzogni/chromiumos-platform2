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
constexpr char kBaguetteVersion[] = "2026-01-07-000124_19ed778850228ccda508c0b3a650f1b46bad2459";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "017c0952ab2c8db036ceeb62a6887343f73b2662cb4bf442420084851a89c1c7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b66480e883ef26e27814f963c2f1fae48629b30267c52196247341869a9b7b4a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
