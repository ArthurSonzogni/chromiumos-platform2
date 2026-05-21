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
constexpr char kBaguetteVersion[] = "2026-05-21-000102_7c7201e0ed0edcca7f6a064db47655aa44cfd9f4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c4af2a4d469b49fc922da3579e875142c27b6550ac6293962b6f64cbb2145cbd";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0fe32e555dc8a8b66c216992f47cba9ee03f1269d663908b8001a4cd00935d4a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
