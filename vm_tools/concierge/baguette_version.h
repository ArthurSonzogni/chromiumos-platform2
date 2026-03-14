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
constexpr char kBaguetteVersion[] = "2026-03-14-000119_d8d8fa70afc7a444de93b9a7ade73aa0308085c1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8084eae26090995655a1e13124d8115905fc1e033756d28896a9a49608207483";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c3097df539fee1ecf4a21dea52a31a48c4ec2a1494b3b1173268332a1e661122";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
