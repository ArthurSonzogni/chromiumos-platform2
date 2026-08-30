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
constexpr char kBaguetteVersion[] = "2026-08-30-000230_a8e0be26281f8cc0b1a11b682fe7e285f13403c9";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "85bfe21cb82a443b2b8602aa8a0632dcac4105e0265e052a9c12b170248c6744";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "eaacdf5483e7b422640aff31cf40448e85b86cf598b55b125973179e9f7d4595";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
