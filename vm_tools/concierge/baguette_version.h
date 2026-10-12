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
constexpr char kBaguetteVersion[] = "2026-10-12-000129_a63a8c4960c2390ef58cf0124bb793ab0b3931c3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3a0731c6cf8b42d7e56d8d3ad0ecc4d493ee75d67910f66ae38e66f73c90f265";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "fffd03cdaee41cd9a236586e491642f15c0671ed00a870158ff2c6b400978332";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
