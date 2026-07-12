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
constexpr char kBaguetteVersion[] = "2026-07-12-000151_a453c45403a550e68ea96169df0774af59d8e2e9";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b384b6834266456494bd72cda04ecba5f16e21651bb75707fda68f47e3c44865";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3f2d38da983c1dce8e238e274bd24b064fd0affd45059c3f3a7d0972b3c8b83a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
