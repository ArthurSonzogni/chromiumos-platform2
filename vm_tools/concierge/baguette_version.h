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
constexpr char kBaguetteVersion[] = "2026-05-15-000111_b01c35f90cc200906af4801606c55a2a803a638a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "818691c0de07f256d37dd02d3be8f8e9e118657e2e5fb23b92be50e1e3dd429b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9ac68f0564cfe202050cea864f8d7fb446dade738fcf617c37cdf8a3832b7214";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
