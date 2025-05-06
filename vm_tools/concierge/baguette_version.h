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
constexpr char kBaguetteVersion[] = "2025-05-06-000112_864e8b7baa86c9c71f381035c68c955dc8b25772";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a0b48f3261f4a0b5550dd6527e0ef395eaffc6aca6562cb9d8fab401182e6c7d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "47a1a920ea853b7c03bda406d9c6a2c50ace39f23447f5cd6b457d050e85a8da";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
