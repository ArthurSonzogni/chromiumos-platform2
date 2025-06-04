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
constexpr char kBaguetteVersion[] = "2025-06-04-000057_d9238a90650aa467df5c672ccba693b864e224dd";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2ab7b2163e24318f2b13441abb8f05f0a93860f29d48a20c23b6484d7396308c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8064a0b6731fb4abc189ce09af7140ef158e5d9e39836aa302f26f8696a35f58";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
