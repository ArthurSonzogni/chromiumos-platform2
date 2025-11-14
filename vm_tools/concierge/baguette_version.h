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
constexpr char kBaguetteVersion[] = "2025-11-14-000615_6e9f96f03e7555134e08716409042eb9f63bef9e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c9409b1b553f6de5353d91d8f2364a0bf02ae5e96f782273702807d2ae174da0";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "31a8e10245aa40b4ee08a4d7340c94976c1112d8800927db833591a187a3c651";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
