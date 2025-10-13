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
constexpr char kBaguetteVersion[] = "2025-10-13-000134_33ba9e87b323c845f369b21807959da192672998";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "aa58667f6245167aea6b2b70f0a199efda6a16a63239dd6390c41f84168a2f0e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "45bb5513d935f4739c60da7349733663505c7cbef05eb25fa60dcba1afeea92f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
