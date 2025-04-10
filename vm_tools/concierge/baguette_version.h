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
constexpr char kBaguetteVersion[] = "2025-04-10-000105_de0c1d0905c47cfc87e1785b963417e1e395ecc1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5b60e515be5ceecac9aa0a4c76de5f53729756a5bb828ea84fcbdfe333fcbcc9";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "837c48808be432c74e3eacaa944ca458cfb6a4761c4a81487a976768a1476815";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
