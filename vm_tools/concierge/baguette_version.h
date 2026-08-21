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
constexpr char kBaguetteVersion[] = "2026-08-21-000122_85c36cae2db1a4f6afcfeb85de6673657d0f4653";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fcef737d1bf009e91855892d299db532cafb65e5a556bc53cd5983eeb66dfe1b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "48b37deefc6767af5baeba0d6afb6827f0921481521a60a2cb2741818a96043c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
