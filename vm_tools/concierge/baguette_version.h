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
constexpr char kBaguetteVersion[] = "2025-09-13-000113_82974a7f9e6816c48225c27fd9a404955ff1d7b7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "58f73448a27458ef06fb19c56bc2be87b1b7d10fee0debfea59c7bf7b3cc7ea6";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c0d811c051d060af1e432e334621912489eb28efbda280cd1855103ed581364e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
