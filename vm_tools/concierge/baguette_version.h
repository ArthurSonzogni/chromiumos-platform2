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
constexpr char kBaguetteVersion[] = "2026-08-15-000110_0c4b947fe1d0a5feea72b6e27a92855a3d97e696";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "28115737f4ce5a4b9b1f1a3da0bac6730f27fbc4eace5dafb75cb850f8b07b41";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8737e17665145a5e2f48044d91258e77868b511f3b7fa20241a8e438253591bc";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
