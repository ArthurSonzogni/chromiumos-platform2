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
constexpr char kBaguetteVersion[] = "2025-09-12-000109_a5357323e05a7893a72bb07a957d2ca5d0f9c80a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "40354c4511d59e8ebe4b32d4a6b3bc37952df5e92748c64ee79d9c00815d1e49";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ab7e256a2f692ca981150e6395f5ea55382b968a230d01838b2bb07ae97f13ff";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
