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
constexpr char kBaguetteVersion[] = "2025-12-28-000111_e3ffc5e7a7e7833d7199251c82fc7f5384a8cd39";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e592c630f41f9af940f054761b6611b327974248e72ce7031f65dc0f88d1e4c7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a0b09e4f5133225141f4fe2337cafb9cb066de4fbd29553fc1a753892f1ef49e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
