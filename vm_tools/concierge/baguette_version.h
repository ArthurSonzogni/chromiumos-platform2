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
constexpr char kBaguetteVersion[] = "2026-10-16-000111_9f456d6e5bd77e6b9a67e9806999f71a2b296a05";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "78b8e885d47e4d18b81e35f7996d6a17c711abedc7219ca42083590a3f325964";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4c4b8a1e90c3e748fb25ed435c1622b393c28258361f5ace173bcc6e7984ec56";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
