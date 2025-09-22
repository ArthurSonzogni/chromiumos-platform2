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
constexpr char kBaguetteVersion[] = "2025-09-22-000141_a466fbc0e50f99aeae196cfc36ce2098cccb4e6b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1c0c74393e4cf71ff269e231011807d55db1bce4c9facd6eb17fde06a9de3d9f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e3a4aca4783f2222556686d7c8ed5a6e776c4dca559a08cfdad1b9f191d6b12e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
