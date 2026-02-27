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
constexpr char kBaguetteVersion[] = "2026-02-27-000117_c7fc086328c9fc7add128a180fe65cbd575ee2db";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6637e015167d6edc58e50f8cef7deb18eb54164c2b1a942f4aaaafc131647c24";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "644c1037610533f7eae8eb8cd9164eb58e5a71a06a77db4104e262785c38a2bc";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
