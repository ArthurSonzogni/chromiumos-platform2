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
constexpr char kBaguetteVersion[] = "2025-10-18-000106_21b784e1ad818f7d39eef5fa9f1f7a038c6a4366";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "566f51d95f68b026d042c96d41759ed2c75ea94f249ebf04d24eb09de4b06962";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "518fb2a66823a45e9fe42af72f498e253f25f0f59297a02b6314f8646e3ab9c8";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
