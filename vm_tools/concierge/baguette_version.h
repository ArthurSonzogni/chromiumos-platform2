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
constexpr char kBaguetteVersion[] = "2026-03-11-000102_614f942dc8d75f7a18377f515a1fa109413ba8bf";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8fae6a2bf62886c1f859ddf8086b01e357f1b9d770266f9e2f3fe93b7787c37f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b9b13a79615339b7c242859847b718484cbce711276a8d383e134e64d9e95110";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
