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
constexpr char kBaguetteVersion[] = "2026-05-23-000121_bd3aa8a330584e69faeeff4efeadea36e1b3a76a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e9e3111990fa5ce9d30ef8640d7ea937d3cc611d7f02bd792d1a5ecdb256e49f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "61a14ff98bbecbe116e6ddc77e22f71a440d4b6960eaa4cfc6adaf5f7d1152cb";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
