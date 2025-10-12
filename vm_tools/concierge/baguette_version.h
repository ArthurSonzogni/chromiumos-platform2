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
constexpr char kBaguetteVersion[] = "2025-10-12-000126_e8572fb464bbe2bb85143cea4e77ec1962593c1a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e5c984b5c876c97386e855bc446d97d34f0eec3ab1b4d307d6c2f78474ae10a7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "88e9c7ddaa6c52725d607bc4c732eccdd05ea8bb2ab71580f03fcfe7fd85d795";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
