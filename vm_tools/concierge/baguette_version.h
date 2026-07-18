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
constexpr char kBaguetteVersion[] = "2026-07-18-000134_ab1b996a3c71c66858237e6586c50d4fa22a14b0";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "433c277d2aff2766ab8761776cc00c5d29457070117f099af0b92caf838114f7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a1a8a5638a20c2ed919c2bd0f181e8a6cbaff2443a4554622baa72fc14abce91";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
