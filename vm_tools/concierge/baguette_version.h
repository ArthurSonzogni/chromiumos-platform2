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
constexpr char kBaguetteVersion[] = "2025-09-16-000121_8419921e155200de2414f605cb1fe3f66da3ad9b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "251ead31cd58667d7d6aaaf8ee29ae2d53cb25a3b738fdcb3d6ea0777abe601b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d8ceee90e5ff5fecbe32b2bdd2027b7d390f799df3694046af70903a71d037ec";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
