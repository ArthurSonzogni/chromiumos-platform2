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
constexpr char kBaguetteVersion[] = "2025-07-21-000144_34070e5e98a45f07581dd514df460b55ffdc1d3f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5432502448888b808b5416b75a347e89838d13d82f06825388135e9fb04c5625";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "58235112b1e95f585da6e80eb788794439c8d271d66ca0316e5d5aef4ac6fc34";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
