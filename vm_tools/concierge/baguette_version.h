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
constexpr char kBaguetteVersion[] = "2025-10-11-000107_c0886ead20092bfca961f41bb5c8e369d8b9acb9";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "be163206e48f60b275547c61938595f63136b854f9213e54b2f1ff6b40093a8a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b1912950b05d967608798b631365a8db822797d1b815e352b1eaf8ea9d71762d";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
