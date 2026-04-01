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
constexpr char kBaguetteVersion[] = "2026-04-01-000111_820a82bee41002fe3708c496b26d6071bd2774b1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6feaa97b57f0ef9c5319eee3808a7b83e6abc08a7d8aeee09bda6352b732c881";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7a2def410053c317b6e1493030cf572edbfe3e565818cd7c3ffd30d759e5aadd";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
