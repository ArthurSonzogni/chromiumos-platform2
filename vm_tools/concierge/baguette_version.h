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
constexpr char kBaguetteVersion[] = "2025-10-26-000116_7f045fc2a99127b3bfa480095c9165c1916cedd8";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5a1c258234c6c70a46c3d866d054923f032f15d4217785fb6ec594109e0117b7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "83f31b9f9c23e74ada79171917d3f4275d8e4d7442a177e80185740b6783a201";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
