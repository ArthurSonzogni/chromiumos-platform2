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
constexpr char kBaguetteVersion[] = "2026-03-09-000130_508d3a920e3fba86a148704bb9db0f451cbc31e3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "74d1e4883e97133beaa9d08ab4662af3c25b6c2addd2fee938447ae04c62b6f4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4d9e7617976c54d49dbdfda49ac8e769072971dc055f5c026b42a4cb93562a11";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
