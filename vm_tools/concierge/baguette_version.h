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
constexpr char kBaguetteVersion[] = "2026-05-06-000103_3eb6cbc502126ec6fa62847747fa9bf2ff3aa324";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c106d7cce24e53f02d5e59a847c538f48e21896f3ff2c756dd34bd0fb0c9ab31";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b6c48e280bdc364b0e0b98783db351a9f1cf38cd790a6631dc8b2a633a888ffc";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
