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
constexpr char kBaguetteVersion[] = "2026-05-13-000103_c712891ef4c0108262b1b5f281b24ec83c444395";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "57392e95d123bc5801ddaf48dc20549c947e3296de28112069a55bd5878c84df";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7bf8d087817aa12709942eb1b7cada97f63b8d2d4a076aeb595ce95decba5fef";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
