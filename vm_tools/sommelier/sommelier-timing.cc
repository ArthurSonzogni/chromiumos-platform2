// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier-timing.h"  // NOLINT(build/include_directory)

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <time.h>

// 60 sec * 60 frames/sec * 3 actions/frame = 10800 actions
static const uint64_t MAX_NUM_ACTIONS = 60 * 60 * 3;

static timespec GetTime() {
  timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);
  return tp;
}

Timing::Timing(const char* fname) : filename(fname) {
  actions.resize(MAX_NUM_ACTIONS);
}

// Create a new action, add info gained from attach call.
void Timing::UpdateLastAttach(int surface_id, int buffer_id) {
  actions[actions_idx] =
      BufferAction(GetTime(), surface_id, buffer_id, BufferAction::ATTACH);
  actions_idx = ((actions_idx + 1) % MAX_NUM_ACTIONS);
}

// Create a new action, add info gained from commit call.
void Timing::UpdateLastCommit(int surface_id) {
  actions[actions_idx] = BufferAction(GetTime(), surface_id, kUnknownBufferId,
                                      BufferAction::COMMIT);
  actions_idx = ((actions_idx + 1) % MAX_NUM_ACTIONS);
}

// Add a release action with release timing info.
void Timing::UpdateLastRelease(int buffer_id) {
  actions[actions_idx] = BufferAction(GetTime(), kUnknownSurfaceId, buffer_id,
                                      BufferAction::RELEASE);
  actions_idx = ((actions_idx + 1) % MAX_NUM_ACTIONS);
}

// Copy out the current state of Frame data and output the last minute of data
// to a file.
void Timing::OutputLog() {
  std::cout << "Writing last minute of buffer activity" << std::endl;
  auto& last_idx = actions_idx;

  std::ofstream outfile(std::string(filename) + "_set#" +
                        std::to_string(saves));

  int64_t start = 0;
  int64_t next_idx = (last_idx + 1) % MAX_NUM_ACTIONS;
  if (actions[next_idx].action_type != BufferAction::UNKNOWN) {
    start = next_idx;
  }

  outfile << "Event, Type, Surface_ID, Buffer_ID, Time" << std::endl;
  for (int i = start; i != last_idx; i = (i + 1) % MAX_NUM_ACTIONS) {
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
  std::cout << "Finished writing " << filename << " _set#"
            << std::to_string(saves) << std::endl;
  ++saves;
}
