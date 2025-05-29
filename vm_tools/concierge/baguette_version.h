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
constexpr char kBaguetteVersion[] = "2025-05-29-000026_bbdb965eff82e7a28dff36c5840166dbdbf0c12d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "108e9ed16698fbf7de29a318ea9c2267660fde3f05b668d7b615a562b1ec710a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4798a45295f89f2e031df552c37ed2cc94bb0b28a7aa32e18cffcaa3e298a28e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
