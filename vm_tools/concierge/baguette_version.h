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
constexpr char kBaguetteVersion[] = "2026-05-11-000116_ff1e80a1c580bf796d16f12f23bcc422b900088f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "84703219b43dbba6ad0880fccb95359dcc37edbd399f09b6d2e3b9a83587630a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "06cfde79a42bcce306e453d2966f497d69409b19390db91b2b78bca9762fb43c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
