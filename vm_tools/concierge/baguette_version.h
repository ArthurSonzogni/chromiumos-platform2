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
constexpr char kBaguetteVersion[] = "2025-07-17-000112_a466175d5313c1b039614c209a15f29141b9fd6f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "62bcd716b1d7cf2925e44a6da16442d158d5817c31da2cb3a11ab08b9eb7eb19";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6ff229e0b70ba422560262a6fb33f2f25cd8335237b432f584cf7e0fb7118858";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
