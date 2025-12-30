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
constexpr char kBaguetteVersion[] = "2025-12-30-000120_e98659848030b9516b9ab420da6aa413d4df1413";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2d49f0ecd0ae212fe3422149cda8d2a5db201a34dfd55b77c933189a082e00d2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "96de904ddb37643f994d1fd2049dc449b55d1d4dbaee65b549088a94c3044713";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
