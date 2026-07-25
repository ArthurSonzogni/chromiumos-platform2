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
constexpr char kBaguetteVersion[] = "2026-07-25-000142_1865c38ce91c992ca6b56fc05fd06ba4f8e16fd7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "76237bcb91e2303c8a79fc79f94b7cb8872cad1e06e1e878c5174112ff6a47cc";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "afe66a67562cbb53bf752ec036e92a096b8138ec95001756911673f7a5732eec";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
