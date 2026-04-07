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
constexpr char kBaguetteVersion[] = "2026-04-07-021416_a5e1948036dbaa2dd6aa30cafc0538571d82eb67";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f3e7edbc486bcd25f04ea475eb5698f6cf07a326b236fcbd6ac552c9f9f7663f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "89b75f53fd4368495e9c32f0ce2941a216d4f16e34c94b926a57c539cff290d1";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
