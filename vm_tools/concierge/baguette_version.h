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
constexpr char kBaguetteVersion[] = "2026-11-16-000125_fea081da836908520874dbf95890820efc0ea9c6";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b99b70538356568ed54078f4e67905784f2c26c240066a538c2d3dc369efdb62";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f638fd75ec0651391e7d98561965839de8e69641c29b87d95dc3965b6c0ee98b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
