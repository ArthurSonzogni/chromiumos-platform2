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
constexpr char kBaguetteVersion[] = "2025-10-07-000132_96ce54eefad4f3946f73497cb51ee37d40bce555";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "eea642538b719d345a987e38b27acabb1dbab5a6b215ae21fa85eebda0b6d4aa";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c547b2890fe846b1331679f802faa3ce9322cbf47be954b182f7beeea5433fd7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
