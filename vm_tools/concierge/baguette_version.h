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
constexpr char kBaguetteVersion[] = "2026-09-21-000112_b1242cfba1a9b8d40e19513a59f191e29e443fcf";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "53f37f8df8c243d393a5ab002635186a3b5de3486660bd4b359e6d47bbcfa21d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0b5ddaebc9ad40a623afd94556add4037154b344bda941647ad6cbde8269f305";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
