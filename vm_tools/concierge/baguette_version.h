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
constexpr char kBaguetteVersion[] = "2025-08-08-000108_a78912c2f3171e73c7810446dd0c03198539e80c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4710b19bf4ef8f1dfb03b4b461c2c60163e06fceafa36a9a4ded4d787dba4da6";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "08c9e83a7eb0f05276618e8a7f0044e0d5314d16d9c7184690046d0b83bf4035";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
