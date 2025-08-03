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
constexpr char kBaguetteVersion[] = "2025-08-03-000124_d761df89fefc01d48b99047fd70d7dbc1ae9a1a7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7973d4c9f4990aacc84fa4cfbae25af4c9e092e3d25e14605194773f46860eb7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "47104ac765ef3253ab850e02c896340de3ad3383c4df0c3f890e0581d9b88eb4";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
