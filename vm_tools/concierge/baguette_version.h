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
constexpr char kBaguetteVersion[] = "2025-08-21-000507_3aabfdbdfe17115b3474cf041062a268c0f09365";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f998b967f4fbcf41d37e9f68d872485a02a28e8fccf908cc0aa1670410f9ce05";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0e868f62fca2ca9253cbae820f7cfa122316bb2a66296715f13c1a9de3dffab7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
