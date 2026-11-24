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
constexpr char kBaguetteVersion[] = "2026-11-24-000104_a498671ff1cc1b33810e6e6258255d5fe1b08444";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2d646c8a097bedcebd34f05a48d2b9f097eb02fd37e7d08e1ae0b6fdb44f8aca";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "fa692ff8131eeb849cf0da572004a5a269f6619ee426dc0425b96b103a0b2c58";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
