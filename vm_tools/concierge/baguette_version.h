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
constexpr char kBaguetteVersion[] = "2025-07-06-000119_ef40a9629a11150877aa4c94174f0699de409584";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "640f532f3440b6e842a865c6659b8c7cb2ea930de9501105b84e5816ac718a8e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a9f5f0ea7baa6742e2f523f2b278a75fc35cad3599965485bccf618e5dac06d6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
