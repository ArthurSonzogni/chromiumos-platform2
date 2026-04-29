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
constexpr char kBaguetteVersion[] = "2026-04-29-000103_61a90efafc2691cf3823903a830af32a52eaebbe";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2fe565fba1d7b54d6ad27fb88932de928b2c753bdee1bb9d7d7cd08958543a41";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f7b1999ac06597be53af77f36142074814e5a2dcc573dd17bcc170ac34931721";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
