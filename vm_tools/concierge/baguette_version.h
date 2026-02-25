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
constexpr char kBaguetteVersion[] = "2026-02-25-000138_2216f6a1c1948d6a5fc79b1084e6ac76600e1d2e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5717ac6c993cda4784cc39eb71058c8fb527a7d90db40c907a844105a0435a30";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "702f3a90a1c4fe3e2effaf7f4541482647a9f0cad1395c4b3d2ced02d13d01ca";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
