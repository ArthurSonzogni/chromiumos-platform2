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
constexpr char kBaguetteVersion[] = "2026-03-08-000113_1a3868ec95f800846b04c73d600b57b9ec31977a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b2fd95b725815f99484e124d8bc98ab43a7ec1c8b2d6e8de960bf24cf81a2b5e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "66f54139ce94b3ce51e332bd680111a3706ad9b215665d125e8f941afab978e6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
