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
constexpr char kBaguetteVersion[] = "2025-09-07-000121_fb4303724dc7474791a5e9b71ec4504e8954e6d1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8ee1c953808c8495bcc2891e663bfc05de200a70097da929a18ed1e5cd5e42a3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "824e2f299eb3c900d6c7b16839c80f1f778b6aeb9f9b8649cc32d3caa8c44846";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
