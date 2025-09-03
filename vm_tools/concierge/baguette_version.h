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
constexpr char kBaguetteVersion[] = "2025-09-03-000108_487a3a7519696876ec12f601c603d96c8eb7eed5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c91c2bc6955e63f7a87b7e470c07ecf114c20a907a097b5bec71c301de477670";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "adebc927c3659e9e628d549dead27d61daf6094c05726037a43eb67973202227";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
