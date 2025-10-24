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
constexpr char kBaguetteVersion[] = "2025-10-24-000103_56849047d77ef379876c2563f0bc6a4558a49f62";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a31cc37d57d8dd77e68ad688bbfcbab2a7d7939e8519f2457236c07eca9adf55";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "dbf8c0491637c3bb19e95be9814d5ed6b160bb2326d5a361488cd41a9e7b044f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
