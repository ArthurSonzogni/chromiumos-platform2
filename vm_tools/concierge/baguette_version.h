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
constexpr char kBaguetteVersion[] = "2025-07-14-000135_f539782e96b97c11975c4a38fb1542a5a6042805";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "be5a29433002ad020896fcc216bd1db9b5a5a02c2d25a051486deeeec6609915";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5c956c23a6b6ab4345f6a48a3ca195f86ab2cfadb9fa97fd646dafe75c075d2c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
