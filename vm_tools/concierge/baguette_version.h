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
constexpr char kBaguetteVersion[] = "2025-09-28-000133_4319d10c664512e6a1de3cb716e7cb516301591c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5ee9df962c6d140b3ade8a5381c00c516bb4d5f5d48032e7c67dc8caee9e0318";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ab99408bf8c8e70246633694dbd31809e083bb02e3ef058c1e4a15c0274517f4";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
