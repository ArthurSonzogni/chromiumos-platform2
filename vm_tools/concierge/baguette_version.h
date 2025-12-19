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
constexpr char kBaguetteVersion[] = "2025-12-19-000100_a44fe9e49144aad404aea0470acc6d58b0aadaca";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a78b8a6e272cd50926f687cb5e3b2bd257fada3c5f8138c61d66ab3fe7f792d2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "bb7a199d935f31389fda9dab872ed5bf29f95c801be0d72227a254f218d9b8b2";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
