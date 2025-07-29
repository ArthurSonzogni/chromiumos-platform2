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
constexpr char kBaguetteVersion[] = "2025-07-29-000111_536c842998c0485f5bc819bdffe78c78ce395e3e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7096591d35f68850122c42eeb070bb6ec57feb8baeadc957f3b5f3b16426464d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0a866bab3c3be1813665a811bb4e607dbf6c13954109fef9aa6b198fb852f09e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
