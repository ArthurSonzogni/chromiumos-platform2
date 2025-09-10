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
constexpr char kBaguetteVersion[] = "2025-09-10-000412_459c79d60b651cdf050c768950ecd753b6083882";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "10849d671824cc620508bb0f7cfae7ce8af5d20e4bdef47f5202f7a98291f462";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f63dc659ee017a6543c45acbdb67ab7c715687de4a11bd1a18a76d1b4550499f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
