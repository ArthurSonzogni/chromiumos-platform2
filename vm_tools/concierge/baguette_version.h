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
constexpr char kBaguetteVersion[] = "2025-09-27-000116_2eef0a79b50edd0a9155d7be18a72bd8f510d45c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "30f30f4c96e3727afc2bec293daf1bed167918326be35f5178b3169d20942f22";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e6539fb7b5e5f90998452402571fb8c877b4b5545086d7ad88105c4d6e577b8a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
