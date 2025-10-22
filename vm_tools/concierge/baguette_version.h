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
constexpr char kBaguetteVersion[] = "2025-10-22-000133_58f2567aae4541332187eec3244f7239e61a5aa4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bc746fb60337867d5b53bf283413c6c9d2e0d24b0616d8a517b52e2d8e39ce5b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "45f40692ccf8f4c886bff636a503e782f956249976625d49c8848113e81f0cb1";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
