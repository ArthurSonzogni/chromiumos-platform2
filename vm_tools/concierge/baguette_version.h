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
constexpr char kBaguetteVersion[] = "2026-06-22-000119_11585a7d4e43496aeb3eb4bd9c9950fbe168954f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "228d557ea9c20de9fd81033f0b212972cec5a576365d424da84e4f71db9d1074";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "2602b2aa5d59d6b0becd3906f03752a12f3969d44ddc40a3ee74583091430846";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
