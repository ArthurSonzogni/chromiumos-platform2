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
constexpr char kBaguetteVersion[] = "2026-11-23-000107_8e3860f8193318c721271fda3e120a7d6ba484f2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "0190cd86ddc6e4201219265cae02cc0d39709c137b0d85693e5c5832e7a5bf1b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "dc5771657b8d74ccbe450c13ed3ceed6810e5b06ab004ce4a8e767b8b0bce584";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
