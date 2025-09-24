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
constexpr char kBaguetteVersion[] = "2025-09-24-000103_528e3c3eb5c0d22665550749ff65f97534952b21";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b2a7d59099a062e7afd2bf3771c41e0d06cbd0fbfd119940525bc25d848ee1b9";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ef0c2b3078d5ae2b299d14dc484e3e45811739b54a4b68353e8f76ae81bfc164";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
