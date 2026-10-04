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
constexpr char kBaguetteVersion[] = "2026-10-04-000123_911ffe434a72eac9dc2f73af0a569348b1d382a9";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "60a63eda198425bdd1b522e727803df7c1058c7c36b74c1559285b2dfbecb1e5";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b806cb7beca08f4cf460986c1fb06c5214486309e7062c3e3f4aecce5190d65b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
