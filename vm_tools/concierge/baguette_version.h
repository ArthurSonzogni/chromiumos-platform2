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
constexpr char kBaguetteVersion[] = "2025-04-09-000248_8f40dfcd8df47c5f931ba0e7664cec2dea7da62d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "280f5cc8a836c2bf4b348dc9234fec8523b244c12f9ef604c2d4e619642e32f7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c79a9c39f6c3963a1ff002a291a8b5d16482aab0a8c994c8236daa43439b750a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
