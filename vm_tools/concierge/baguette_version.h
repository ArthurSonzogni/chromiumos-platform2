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
constexpr char kBaguetteVersion[] = "2025-06-27-000124_d599ade5bc7267dae1cbc7c8d3fe6f3b782d70a9";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9254a9d4dda9ca46c748a2ca266aa1ce20a3ba89471b1541d9ab118f4ec71074";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4f30edc40efab66ec7a0f5109d69f06492a4b7cb9b7d92e5daf8f9503cf5344a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
