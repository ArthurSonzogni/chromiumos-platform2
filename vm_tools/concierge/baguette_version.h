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
constexpr char kBaguetteVersion[] = "2026-09-07-000137_1a1d2798a0ded93f1bde14f4bb0d1cb07a781227";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "440c2f6af9a69cf9bcbb60a7701df7b4316931cf2c0b84b37e84e98d19877a67";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a3c608be7adcea5ef612cf008fa94fa653e7b62290cbb3687497e0a153ef16f9";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
