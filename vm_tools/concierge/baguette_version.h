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
constexpr char kBaguetteVersion[] = "2026-05-20-000105_3dad4297731618829754076de1c4a645ca8c21cb";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8e7fe284fbfc4c89d7ba6be5ee4bfd4b5aa0f98d4c454d6c1b06bfb93a40a241";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0c8999f12ca563c32d4d852811f66ab4990e034e224107408b510f57d4baf0dc";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
