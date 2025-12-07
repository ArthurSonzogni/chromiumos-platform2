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
constexpr char kBaguetteVersion[] = "2025-12-07-000101_158d0fb233b7488608c41bdec197e73bac1e124c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "20cf0af3eb3bae33671f0dc06f2a71c4a8c9aea7aa483bb9dbb1d51c576407a3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "14a579e551bb5213a9d622230266d83783ad6184f5ce193b7c6e1306b9eaa16a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
