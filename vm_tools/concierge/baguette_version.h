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
constexpr char kBaguetteVersion[] = "2025-10-14-000106_0051e76524edb9a4a01aeb8c21901ec8c954bb2b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "724f6ab561edc8dc5805df3a3dda8e0e46fc72171b91edad648d9371c804b3a0";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "eb2d3758fb8ece3836735ecfe3e10ebad9cf55285012307ed28110b4fe35f52e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
