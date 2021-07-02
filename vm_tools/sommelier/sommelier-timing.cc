// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier-timing.h"  // NOLINT(build/include_directory)

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static timespec GetTime() {
  timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);
  return tp;
}

// Create a new action, add info gained from attach call.
void Timing::UpdateLastAttach(int surface_id, int buffer_id) {
  actions[actions_idx] =
      BufferAction(GetTime(), surface_id, buffer_id, BufferAction::ATTACH);
  actions_idx = ((actions_idx + 1) % kMaxNumActions);
}

// Create a new action, add info gained from commit call.
void Timing::UpdateLastCommit(int surface_id) {
  actions[actions_idx] = BufferAction(GetTime(), surface_id, kUnknownBufferId,
                                      BufferAction::COMMIT);
  actions_idx = ((actions_idx + 1) % kMaxNumActions);
}

// Add a release action with release timing info.
void Timing::UpdateLastRelease(int buffer_id) {
  actions[actions_idx] = BufferAction(GetTime(), kUnknownSurfaceId, buffer_id,
                                      BufferAction::RELEASE);
  actions_idx = ((actions_idx + 1) % kMaxNumActions);
}

// Output the recorded actions to the timing log file.
void Timing::OutputLog() {
  std::cout << "Writing buffer activity to the timing log file" << std::endl;
  auto& last_idx = actions_idx;

  std::string output_filename =
      std::string(filename) + "_set_" + std::to_string(saves);

  std::ofstream outfile(output_filename);

  int start = 0;
  int next_idx = (last_idx + 1) % kMaxNumActions;
  if (actions[next_idx].action_type != BufferAction::UNKNOWN) {
    start = next_idx;
  }

  outfile << "Event, Type, Surface_ID, Buffer_ID, Time" << std::endl;
  for (int i = start; i != last_idx; i = (i + 1) % kMaxNumActions) {
    std::string type("unknown");
    if (actions[i].action_type == BufferAction::ATTACH) {
      type = "attach";
    } else if (actions[i].action_type == BufferAction::COMMIT) {
      type = "commit";
    } else if (actions[i].action_type == BufferAction::RELEASE) {
      type = "release";
    }
    outfile << i << " ";  // Event #
    outfile << type << " ";
    outfile << actions[i].surface_id << " ";
    outfile << actions[i].buffer_id << " ";
    std::stringstream nsec;
    nsec << std::setw(9) << std::setfill('0') << actions[i].time.tv_nsec;
    outfile << actions[i].time.tv_sec << "." << nsec.str() << std::endl;
  }
  outfile.close();
  std::cout << "Finished writing " << output_filename << std::endl;
  ++saves;
}
