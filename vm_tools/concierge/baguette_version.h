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
constexpr char kBaguetteVersion[] = "2025-11-18-000106_cb0cc59c4a577f0f8668e12613ca453a56153047";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6fcbf5521c5b354ee20066dfb4ae8b7c19f5351ae5890b66f99257f4ff6f3999";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0f98dc82347097f5853d5a069e5953ba86e9c8cb23b18c31dd477909e2e3d72a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
