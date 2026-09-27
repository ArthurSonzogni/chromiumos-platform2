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
constexpr char kBaguetteVersion[] = "2026-09-27-000108_6eb20b39760ce1da5443562dfe15be75a2231734";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "85781667c875210bcb6913e207995d0491101274cca6df4c370f5729a4bf1a03";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ab68f830b3e9878038296b210586d4e0a3cf0071338c027ff31a5ecf62ea3d77";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
