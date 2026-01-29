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
constexpr char kBaguetteVersion[] = "2026-01-29-000059_863d0023be90f9114c852dd3a649dfd3d2c53f6a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8d6ca354aa35eb711ce5659955bd496b6263985f50beff6c118572fcb929966b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "33cbf990285133d5859c640d08ae0cb64fff6de10986351ad902c2b2ce25318e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
