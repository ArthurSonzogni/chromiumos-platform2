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
constexpr char kBaguetteVersion[] = "2025-12-20-000106_5b0b9f8e03cb025c21ffd05216ee82390ccadeac";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fe59a3fb7a5326c15c6f8fc71b24af80845cdaba014409cbf8c34061b0b31a3e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f20bd35be918891f3d599a6d581a76a26e1f02b6b1c3fa4e636f34ca949dac1b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
