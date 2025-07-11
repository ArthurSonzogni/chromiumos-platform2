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
constexpr char kBaguetteVersion[] = "2025-07-11-000121_9ec0bae8f16728e00161f122cc6fd7d779baeb35";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "da22ad8ae0612d33f18c158c3b478eb709fdc3d39dbba76f0ae50b374a42a810";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3fda6efaf7595be306de7635abb95fe81fbe78155a2bd70f46a027b249a53188";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
