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
constexpr char kBaguetteVersion[] = "2025-08-18-000131_a93acbd8ec82e06d6e14d9d55d35a59fe506cbd7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ab74fe71879f3360b5630e0b0be7c496fc1d2958aacef5f53201f8f165e05d1c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1fdc7809fc81a2f9bcda4df693cec1c392506d141a5ffaf51658fba41cdfd65a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
