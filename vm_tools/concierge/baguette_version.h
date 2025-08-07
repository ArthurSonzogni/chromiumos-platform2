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
constexpr char kBaguetteVersion[] = "2025-08-07-000120_7b16dad29b8e203c93164429c52506947dcb7d7c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "420e21f0e084e5b80881a8d1ba1312fd907f576039e870dd1920e42bcd6c4a04";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "2c04f47f2ff20e67d857d88b7dffc00af4161055ecd3506dcf2651a2e3b0b0b1";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
