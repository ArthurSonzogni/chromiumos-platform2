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
constexpr char kBaguetteVersion[] = "2026-07-30-000107_b57000d3f131bb6c5315d783e6f5eb036e2271fd";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "0eb1a340d2be6364ef34c463b76d7f9531deb30b1adb3a9bdc21c3c9832a1aaf";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4a440f919e97ab8f525a90f8b200e92077679df5c21c8f45be61c6b44ddc280f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
