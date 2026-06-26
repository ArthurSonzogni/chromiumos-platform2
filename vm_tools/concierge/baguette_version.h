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
constexpr char kBaguetteVersion[] = "2026-06-26-000116_362404e596160add78f63bc42ff2081b91941af5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "313e3b9d0d7abff066f415872a5785864ba04c16cd468a8f9110e8bdded3e5a7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "fbd22a7ca4a8c855f97f9791e30981ab5ee910de1305ea4dc59d11ddc1eebc09";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
