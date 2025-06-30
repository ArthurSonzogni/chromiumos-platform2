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
constexpr char kBaguetteVersion[] = "2025-06-30-000143_bac9e37884d736f2b3fc11b4b5bd74f5c9b38a51";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "47069917770647a3695834fbe6d9d0a7bd41adececc28be0e2579d309ac9f724";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8f5234d5d70d479c02126499a06845c1072fdd921374944ff39294dfa7ddae07";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
