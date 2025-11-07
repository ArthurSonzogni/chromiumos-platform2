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
constexpr char kBaguetteVersion[] = "2025-11-07-000129_ed83332d9d1becc177d7a227ac85ea0f396d95e6";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "36ed9641a5bd8321572205e5acdfda412daf28b3702e5ddc6957daa542533e39";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "602466d7d01086b87b19f8a4ccf6cb75c32350ac91f9a3c83cdfa964e0c4fed7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
