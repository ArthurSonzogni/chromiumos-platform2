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
constexpr char kBaguetteVersion[] = "2026-02-16-000132_b99b839b843609d20696d7df085a1901c3fd7d13";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "90803e35903f8091641045e9e9fa48b034432b89f1e71585931d43d6881497bc";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f837cab9d8d4f5a0cc1bcdf8fa8ecb5e377050af84eb2dbfcfee35f6d7902108";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
