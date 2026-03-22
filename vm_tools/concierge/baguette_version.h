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
constexpr char kBaguetteVersion[] = "2026-03-22-000124_c4c2468e01f6b37c97d842c9981b1bafb71d751b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e90794d9c23634353ec6404b5ed0fa098b3295b5ac60f7d578afbbbb75d60c2e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0c5642c7c3dd452348f75e79b77aa6dce32c61a6d930eaa7a6bf8322995f6fa2";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
