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
constexpr char kBaguetteVersion[] = "2026-06-09-000115_30a5abfd06297e0a86d2e4c4da08d98058df54d5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3eaad8a787342412921759dcfe2fe54dc224d44d730ba5f48264d53df23026f7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "064f65e44009e23169a52146d60dc5dd7091e75be0c3ec9b33f16e8984b2a132";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
