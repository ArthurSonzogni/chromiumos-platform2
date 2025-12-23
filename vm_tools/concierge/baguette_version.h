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
constexpr char kBaguetteVersion[] = "2025-12-23-000111_1e5b465dc9e9cc567cd715c28634b675b1a2a858";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5072564e6628460ad2e20d4703bf103d6afb92cea0825f89ba626c20584b930a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8fd0e429d7973069f35642e98abbe83b877b8709527ed08a6ac9f3a9b061bb87";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
