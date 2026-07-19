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
constexpr char kBaguetteVersion[] = "2026-07-19-000139_4ddcdd7beb9109cb9b541bf17e87069f040a6fe8";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a4c13239fec9491d6b24b4533a9dd535387eaf294b5866079a1058a7fd67bc5c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "141f6771126301d388f6db3085b1c8dc25d287e0910873a930146dd3ac0d5d6c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
