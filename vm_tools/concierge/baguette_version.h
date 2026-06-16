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
constexpr char kBaguetteVersion[] = "2026-06-16-000105_51a19fe0bc10838de26b97083cf0a58f90a7a7de";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d88ae904473322fb40ae081d02a18f69620eb07526f66eb8a9888ac175fa2e57";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "bd6b043b3ab011f58c990ab66fe8cb8678fc36d37094a2064cd01ad3718615d2";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
