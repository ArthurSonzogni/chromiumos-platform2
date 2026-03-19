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
constexpr char kBaguetteVersion[] = "2026-03-19-000109_c7e9bb8729916d9caf48bac8021ea5717da3b655";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c5e6633cbe13e1583b8e619cd631d000492d2c6cccb535cdbb4aca97871129ec";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "433909ea0b16a2b7864145da1f6fba6a540c3345d4946412493f8775a2371cbd";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
