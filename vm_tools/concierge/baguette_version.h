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
constexpr char kBaguetteVersion[] = "2025-12-26-000129_d82d693171cb21650285f613c8e6a6e120c3f7f0";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4eccae77de2c1ebafa2f86be669ad9d240faf27a1d3135d66faed895ce29ee82";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e9183d79a247cf59c44c67a0ee35f63b8740ff154ee2d144fedd6f9c39348c60";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
