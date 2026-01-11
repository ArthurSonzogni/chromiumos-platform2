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
constexpr char kBaguetteVersion[] = "2026-01-11-000119_b725a38e75a0df915449892e9ceeac0ff01a4cf9";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "85d8716591cd404d5e79f8192271141dad46325e2f3b839816f0df9ac313d075";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a368fe3dad6cff66b1d07f6a6b75edbc29a856e2213571a40e80eceefb1820ed";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
