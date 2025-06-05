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
constexpr char kBaguetteVersion[] = "2025-06-05-000102_b9cef3efdc7a038c8a9832e7b571a4147f8fdcf1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "99b235b1d949f82dc50f012139973fa41ddbd7aae22bff78dde9981543bf060b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3f9d0c249c7381b0f2f278d1f88224b4a911f8962452affa64d92fedea1d40ff";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
