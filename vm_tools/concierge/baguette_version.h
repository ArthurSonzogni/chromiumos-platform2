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
constexpr char kBaguetteVersion[] = "2025-04-24-000101_0cb24836997bfa224f17aee803c2879f428a3596";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e125e5d7151768833a377b00b4355f5f7cb8be8c38f8a566179cf315c1d33317";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6396d307421aad24f33bb1c336d9d1a79ab21aa2dd4be9c13d081dee2a7d2221";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
