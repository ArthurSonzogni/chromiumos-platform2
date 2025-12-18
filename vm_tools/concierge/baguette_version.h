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
constexpr char kBaguetteVersion[] = "2025-12-18-000104_ff1cb8c4abfbe76da96f38f839657a23fce97cba";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "cb3b3500e9152f73e0d875693e47c66c1121dfd03390bc3d5e678575145d0480";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "653a9a1597a75de82942eb4e7df379ac6a74b7c950be100d5813f1b5f0125ea7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
