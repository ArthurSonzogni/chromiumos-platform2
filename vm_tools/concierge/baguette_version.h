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
constexpr char kBaguetteVersion[] = "2025-10-25-000103_e6e5cf8d999ba5952ff27f005123f36ec35a9e81";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9edc2d77d6b9ec0b5f8dec6f5221829040052fac473be3184200b219fc1eb5a2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5dc59a4478ca42879d55be80357525446467df530f831859a55c1d3c89d13bb9";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
