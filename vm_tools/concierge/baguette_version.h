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
constexpr char kBaguetteVersion[] = "2026-07-09-000103_3370d7f8eddfeceeea2e4aa9ddc338825bf1b2ec";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ddc6dea377f2949ed79b592be506f49c7ea014456df78890420fbdab593908c0";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "38eecc539a56e3f00f1563e88aae312b574b2312cdc207205610e6abfc205ccb";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
