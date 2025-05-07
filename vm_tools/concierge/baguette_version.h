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
constexpr char kBaguetteVersion[] = "2025-05-07-000104_e34b2e73a01a7c5c4cc2f4958e27cd6f8e470877";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c54c75f7a67ac5703ced3735327b7e51ea95233048c77c1083fe6557b24fe90a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8e982c615ca4d64667563b8d62190b8fb72f56427b83bfa5dc082c7f2e40dc36";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
