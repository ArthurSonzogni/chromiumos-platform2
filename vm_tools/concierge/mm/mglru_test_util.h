// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_MGLRU_TEST_UTIL_H_
#define VM_TOOLS_CONCIERGE_MM_MGLRU_TEST_UTIL_H_

#include <gtest/gtest.h>

#include "vm_tools/concierge/mm/mglru.h"

using vm_tools::vm_memory_management::MglruGeneration;
using vm_tools::vm_memory_management::MglruMemcg;
using vm_tools::vm_memory_management::MglruNode;

namespace vm_tools::concierge::mm::mglru {

void AddGeneration(MglruNode* node,
                   const uint32_t sequence_num,
                   const uint32_t timestamp_msec,
                   const uint32_t anon_kb,
                   const uint32_t file_kb);

MglruNode* AddNode(MglruMemcg* memcg, const uint32_t id);

MglruMemcg* AddMemcg(MglruStats* stats, const uint32_t id);

testing::AssertionResult StatsEqual(const MglruStats& lhs,
                                    const MglruStats& rhs);

}  // namespace vm_tools::concierge::mm::mglru

#endif  // VM_TOOLS_CONCIERGE_MM_MGLRU_TEST_UTIL_H_
