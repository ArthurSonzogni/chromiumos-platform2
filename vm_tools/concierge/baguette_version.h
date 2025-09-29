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
constexpr char kBaguetteVersion[] = "2025-09-29-000125_d2ba5ba461ad03a61106afeb7a00bec39f9b5eb0";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "90204e4eaf7ce1484db7f8bb15e3175a7f6acae9d687f36fc3b8b4b27344cd99";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "73c1f6477c2372a58fbc460deae6a6ea204a49e9788cfa097071da72991030d3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
