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
constexpr char kBaguetteVersion[] = "2026-10-02-000130_f85542005b9b085ab866154883ed3d157d173835";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "451683f77c848ddb8dcfe4694e832b37bd13d8be5d8cbb7426676cdcc49b4cfa";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "dd6204b4fdbabfbeee7bfc48b85f893f0512cff41cf37ba04818cd063750a856";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
