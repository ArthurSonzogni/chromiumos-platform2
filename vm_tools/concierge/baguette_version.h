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
constexpr char kBaguetteVersion[] = "2026-08-02-000213_453612edd3ccb20df45bb3484fd4039444e68fe7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1ab309838e50d18c1213f91f23702b5dc75d0847e4ef7c541f3d79dd436dce8c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "cd0accb7a29cdc757a0106c83768523c94e84e2fe195ad1069a32fac01c87f62";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
