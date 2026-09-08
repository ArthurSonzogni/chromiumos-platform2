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
constexpr char kBaguetteVersion[] = "2026-09-08-000103_63b5467e409976a451b8f7a0cd01996bb61c89fc";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "96b3b70775711f75d549786056e29b83e849ba6fc4932daf822d0f87fe1ed98d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "edee332e80804f7bf0a3f7743fba8e178cb1463ec84d49cd1ca7bbacc32788d9";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
