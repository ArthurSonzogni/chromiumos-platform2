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
constexpr char kBaguetteVersion[] = "2025-05-25-000135_2c8e42047f0f02abf7e4424eae6789c8df1d3202";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6c8104fb0b580fab4f76547f203ef2167bcfe9f9681dbdf3a91baf8a4474b3fc";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3ef9ce446f669066fce53b09036e0dfc37c70eaf52b5b5d05e8751a2339aeb11";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
