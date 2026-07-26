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
constexpr char kBaguetteVersion[] = "2026-07-26-000118_77efb026127582a631f28c2dc7a48650b5f5fb87";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "969f1499301c71187d251f8fae50b02ffad1f9fe6b99e8afc65c6495f3da10d4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6ce0e318d4fd61e7a4fb074082ed4243c818c4221882d0fe23176bf2ee6e644c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
