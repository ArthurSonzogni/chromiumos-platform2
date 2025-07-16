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
constexpr char kBaguetteVersion[] = "2025-07-16-000140_3e0af2e381d98e26518a171b062819b7c26d35e7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fcc8eb1d10fff7f4392c3a88a03c8698204726ebfd871ce1ec6da4695e9b39a5";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ea26a0f56c3ea8b12c8b9c143921dd8a09e1a240bc78dc0b31357fdf6484917c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
