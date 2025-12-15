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
constexpr char kBaguetteVersion[] = "2025-12-15-000124_49260db4132268dfec73889588bb14573872cc4b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d9a8653419fe032848d624102e5b190ee1546c0368dc3902c3bcee372830edb8";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "89659eb0f1a04cc628d1035f5d5f310e1529a063042a92369c60b352d9af27b9";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
