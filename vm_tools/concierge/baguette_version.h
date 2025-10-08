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
constexpr char kBaguetteVersion[] = "2025-10-08-000111_1ca7b04a98c46da2844f77f58be80da06198c815";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "322123fd5f11b5de96d498b63e43fa258a420fe7244ffad23c90b74648f53623";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "787a5e02769e65e3b01aba09d5a1933f0e0e9a1b8c1528311edf17a78e614374";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
