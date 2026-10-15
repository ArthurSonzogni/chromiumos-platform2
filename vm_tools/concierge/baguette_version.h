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
constexpr char kBaguetteVersion[] = "2026-10-15-000113_1d7a5b8876cefab35cf1ce01a9b47e8b4b3272b2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b5b974b96af58ad1419facbb41a2c56ac9f0b1d7f626127e0a22247812f52e14";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1c4cde5bd14977a010788f81ad714b8f9b711bd0c1dad4d35100c6d4e1f0399e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
