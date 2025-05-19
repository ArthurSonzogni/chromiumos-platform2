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
constexpr char kBaguetteVersion[] = "2025-05-19-000120_b244c23e1a80c2dc884b2ccf223b464ee8f43096";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d7d62df0ba01f0e3d183d23b1514251780724ce4fa13756776ac4e097851fd17";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d23ce5007d57977c9e5faf1f9ca4786e63b70d2831260fed685534a52ddb3da6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
