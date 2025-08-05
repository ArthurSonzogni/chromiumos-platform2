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
constexpr char kBaguetteVersion[] = "2025-08-05-000114_bc84a25ac5a31885c04257fe7a9061d4ffb587a5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "0a91a2dc9cc2ab57c0e40acbc3c9d7ab2c7bbfa0f396b5781d81d3b0a5e9a4b4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1eae6471cdd6b7cd38cc3863ae72d3921aeb497dad1647cad280e294d76b86a7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
