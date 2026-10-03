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
constexpr char kBaguetteVersion[] = "2026-10-03-000108_3920e311a0cd34370e35fa29d2605b48573e9996";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d55aa1e69d5fb69f5ec9991b2623bf802a307c49652b320f9c710ad70b4ab611";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "aca6594889fc53640a05a0d525a729e6764775b34a7d755146c4b91aff2e26d6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
