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
constexpr char kBaguetteVersion[] = "2026-09-29-000108_df30d174566f7d1c5e01d879d2639cbc81be1bab";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c738fd421e0948c6ebafb788ebb26b162ec94764c3af4b16e586458f7d29798d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "09d4d91692c09dc4682dddf1e4962f366fd825630ae2d21172dcf4a490281b78";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
