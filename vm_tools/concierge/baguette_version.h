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
constexpr char kBaguetteVersion[] = "2026-03-21-000125_9be73f2aac4530d65be95ae149df518819481cf2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7acd3b62c4a3261ff048be28da825baa77402574191d1002f936ca35aa5bf2e5";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "933adf26fd3ade29d4cb1ae4e4ea8c9bd43e2813edd93e35f090593e4efe729f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
