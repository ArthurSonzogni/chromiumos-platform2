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
constexpr char kBaguetteVersion[] = "2026-08-03-000216_ca5b24a0daeb7ea2b254e4ba7e78fdfdee1f7024";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "07f8f721f854c9845a3f3fc118c556af3b55ac654f5c56c9a36758fd3e5fe628";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4484048da9572888b4b41af35f56275ec74d7e5d317eac65f51496ebbfb62f8a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
