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
constexpr char kBaguetteVersion[] = "2026-03-17-000108_cf9f91a594227140b4fc6d71d56c735e56932503";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f283c76065aebef4ece872d758c1b85568bdaf3c3476031fe961348abc2377ce";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a25668df282a3336f22d68ff1dfb047697145bfc193bbd9fe59be36b5406572d";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
