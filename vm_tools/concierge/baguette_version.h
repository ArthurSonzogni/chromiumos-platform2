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
constexpr char kBaguetteVersion[] = "2025-07-30-000123_a08bc9d18dd34f794afc70bc42c2b6f7a7d5e64f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9778e43949b86e7961245ca9b8b220668d228696d6a945c983bc6cb59256a290";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "97cb4aaf8b8eac59f1bd004009b183449f3b5f67de1c7b1a1cfc9f897ac25c07";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
