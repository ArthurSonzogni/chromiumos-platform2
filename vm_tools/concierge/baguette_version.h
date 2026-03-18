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
constexpr char kBaguetteVersion[] = "2026-03-18-000104_32156da079aaed6ea9af0136e210d10fc35eb5ff";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7c4222ed9a34decd2df5257f3ee757e2f2b50d6974e872d879c69f817c27176f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a6db45eef045e0900bfe5484cb9753b462e13588372d1cd80881571c44961af3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
