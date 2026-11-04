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
constexpr char kBaguetteVersion[] = "2026-11-04-000112_a367aecaa5a31ebe0ee53944298e5c302e14b195";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1c65048988178cb8db5ce9a51a30ad7229daf326b44dea3b852e4269d6997e36";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8df4b47f16ab56e35a1bde6dbb2653842fe838458a2123d4ca2c146248fac924";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
