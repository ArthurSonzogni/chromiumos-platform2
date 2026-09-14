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
constexpr char kBaguetteVersion[] = "2026-09-14-000132_884fa9d33c972de07846b5e51a39be838fe88136";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7580524d4855006a6e3980a9db03db189eb8054aa419d35eed76f61945856757";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "005605226fdf1662524b078e6b735233245007b219b508a93afcace994160717";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
