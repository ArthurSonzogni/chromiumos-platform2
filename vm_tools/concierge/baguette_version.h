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
constexpr char kBaguetteVersion[] = "2026-09-22-000107_53231aff042e5b0274a681cd63abc96842dd065c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "48bf8888c24eb11fff8c7d58f1add62617c09418b244dae98d7126f1c442492f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "18ab9a7e08157235c50657825f47560dbbbb58a96d9f0df07af0bf5a4946bd72";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
