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
constexpr char kBaguetteVersion[] = "2025-11-28-000131_22af6289eb78f7882cebdfc872dc96dfbe51ba92";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "dae190e906cc8993bb57368cc52b3aa84a36668b4aad86091d5e0f60211ccad8";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "219925bd325525a2f307becdbc1f8245a9f26b6be7071d64c4b705c1a1e6561c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
