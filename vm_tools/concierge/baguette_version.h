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
constexpr char kBaguetteVersion[] = "2025-09-26-000107_83f0b59b580237ef89226f18a616dea3508d2041";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bcf25fe700136a5165b6ff4d6cdaa368ce1a8e39c6ccf80a68dec7db84b4cd7e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7a349459dc237deddaaef3f248086fe3999a887fa0a173e3d871d6848335df06";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
