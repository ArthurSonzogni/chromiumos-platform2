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
constexpr char kBaguetteVersion[] = "2025-10-28-000110_04dbe7eafc6696943264946cd93925e84b69a578";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a77730bbacdf13ca10e15d6cb63d9d34b8b713185ac75f8adc026659097df0e8";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "cbb7d6fefeb17f54bda9411327e26e73e3738c190aea67b1ab01897c1158b8dc";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
