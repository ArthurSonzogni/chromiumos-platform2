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
constexpr char kBaguetteVersion[] = "2025-06-13-000110_46c9022d9798b9fb04cc693a059bf875b3f0ff80";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ed39e6f3e1fb928612b1952c6bd96ec3cefb2903526cc502e9f5af435c9f31de";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "837d429524e10369eeb2f83fcc9ba37ac2325f78095f90a8fd81f92abb1da74c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
