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
constexpr char kBaguetteVersion[] = "2025-09-11-000110_a71ea159a3c9856fb9b063b4a697b015a4dbe133";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "669a8bed7b5dd40b4f16683cbed18dfb2cb04250cbd95279d32dced3c1297d36";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "722df2a000bc1c827ed0bf9b1072a5558bb0726471184de2dcc12b8eb2170e68";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
