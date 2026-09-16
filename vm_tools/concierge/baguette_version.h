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
constexpr char kBaguetteVersion[] = "2026-09-16-000105_4cbb738d1af9dfe7b8590b3ce447af06ee7131be";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a7db01abda77b43d7f231f75d15d0417013745bffb158e2ac43f6bb799cdea51";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8e0c8d75938bdc037315e5d016549e42f4940883a6bcfb732c148eca72f56c45";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
