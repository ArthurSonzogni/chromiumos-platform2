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
constexpr char kBaguetteVersion[] = "2026-10-05-000121_a69d67adfcf322b103d24dafe058e33607affec1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "53652416c91f36a2240efcc2a89a8d0636abd0852de9f96263e66057e66dce2c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ca6f7631046bae2e261faaa16b34c31fedda20e83d0370d2824cabaef0afe91f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
