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
constexpr char kBaguetteVersion[] = "2025-04-26-000120_d41468f931aca2127e8f1c2537c5a0d655f1f863";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "59f91bdba4d1ada40e3b23ede5c0789a3a226bf01987926b1b0e978303a81e40";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e6345110fcb4b5b60287aa1299e6d6152f8e9134b29730e9206ba421d6351395";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
