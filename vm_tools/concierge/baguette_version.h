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
constexpr char kBaguetteVersion[] = "2026-02-13-000107_4f537a48c4c07016ed7e10b4eb9eb78d6623d0e6";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "320e886b49d83c870bf3eab840459006506d2f059d11e54f5e6c73cede9dcd9b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "2e57836ef43bf932f83c57b5b9cec81166db5416b72ee45f222c42f8aa816682";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
