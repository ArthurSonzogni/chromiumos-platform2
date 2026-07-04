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
constexpr char kBaguetteVersion[] = "2026-07-04-000156_3401ecb45ec88e957803aa1ac019f6f05aa10fa2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ef6b9fd756938e6ba841e887cb8b51889647ace5a9a90718ca939353c89949e8";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d083ef779d6a51c080c3ba660f207dda510d18a6de6859357ece5ba5e8c87a75";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
