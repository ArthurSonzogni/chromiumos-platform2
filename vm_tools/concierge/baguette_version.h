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
constexpr char kBaguetteVersion[] = "2025-11-05-000103_8a160d960945e63db96e52e801fcae2450896695";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1696414ac44958a9c02104c4e0a61505b3233cd47fe3ffd5340ed3aafb7d9de1";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "390db55582dff89605ccadcd67b0e8aee077e52c426bf3af0a4e555d9f412e44";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
