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
constexpr char kBaguetteVersion[] = "2025-10-02-000102_720e2ea241b42fd96785456c3d9c2a063ff8041d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "786c5ea2995619d106ebb3cc806a5bfe5f1ce51cbd546a7a6e9863b650d11111";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "098231f53f7d23ea25427a1c07acb28f0c1b8df47867f8c6eb5f0ce8a8ddf52f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
