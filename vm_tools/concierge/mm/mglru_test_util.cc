// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/mglru_test_util.h"

namespace vm_tools::concierge::mm::mglru {

void AddGeneration(MglruNode* node,
                   const uint32_t sequence_num,
                   const uint32_t timestamp_msec,
                   const uint32_t anon_kb,
                   const uint32_t file_kb) {
  MglruGeneration* new_gen = node->add_generations();
  new_gen->set_sequence_num(sequence_num);
  new_gen->set_timestamp_msec(timestamp_msec);
  new_gen->set_anon_kb(anon_kb);
  new_gen->set_file_kb(file_kb);
}

MglruNode* AddNode(MglruMemcg* memcg, const uint32_t id) {
  MglruNode* new_node = memcg->add_nodes();
  new_node->set_id(id);
  return new_node;
}

MglruMemcg* AddMemcg(MglruStats* stats, const uint32_t id) {
  MglruMemcg* new_cg = stats->add_cgs();
  new_cg->set_id(id);
  return new_cg;
}

testing::AssertionResult StatsEqual(const MglruStats& lhs,
                                    const MglruStats& rhs) {
  if (lhs.SerializeAsString() != rhs.SerializeAsString()) {
    return testing::AssertionFailure()
           << "Stats are not equal: lhs: " << StatsToString(lhs, 1024)
           << " rhs: " << StatsToString(rhs, 1024);
  }

  return testing::AssertionSuccess();
}

}  // namespace vm_tools::concierge::mm::mglru
