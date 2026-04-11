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
constexpr char kBaguetteVersion[] = "2026-04-11-001259_c1a6f0d8c84a573728cd89ac0c4ca58754ea19e5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fd5e1318c84910cdfcb97ff3b72482e564b404f7b6d380b8e256bfdd4905f47c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "15c2ce42fe957673bbfd35da2901cabef841744383e664d9c9b2cfc4c3d53d38";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
