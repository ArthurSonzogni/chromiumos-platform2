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
constexpr char kBaguetteVersion[] = "2025-10-06-000127_83c72dbd3bf1e39f207184f332b6fadafddad391";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6a7c71aeae309dbc31e9a3d4ba063b7561f1eafe62ca40f96678295bdf1ba166";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c888de898fa1af1368237000caa8a6a8b3f5a879e275205062e6b14372f0d77b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
