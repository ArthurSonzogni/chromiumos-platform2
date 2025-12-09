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
constexpr char kBaguetteVersion[] = "2025-12-09-000120_c8faee9c0ae44e6bd2ec890c5c733644f356b66a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "dc7328702f624f386ae3949dea859f33aeecf78a84ec44b8719c68d5392e16bc";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "407e701266fe9e004dc9d2acff69abe53c5cc282884e2ee67a9e497cc6d6e652";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
