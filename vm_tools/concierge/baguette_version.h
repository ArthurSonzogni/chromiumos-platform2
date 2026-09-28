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
constexpr char kBaguetteVersion[] = "2026-09-28-000112_7f17738f71a11c35ac7a8c08f0fa80a79b34d238";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "054f498d35202a0eeeea4477f87d3e9eaad302f300a4d10bf0a5f2e4809da3be";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4ad0af797cff2c5d84ada7cc4deaf9e97a19ddbe999a918a4df115251ef66779";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
