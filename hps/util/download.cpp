// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Download file to HPS.
 */

#include <fstream>
#include <iostream>
#include <string>

#include "hps/lib/hps.h"
#include "hps/util/command.h"

namespace {

int download(hps::HPS* hps, int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Arg error: ... " << argv[0] << " bank-id file" << std::endl;
    return 1;
  }
  int bank = atoi(argv[1]);
  if (bank < 0 || bank >= hps::kNumBanks) {
    std::cerr << argv[1] << ": Illegal bank (0 - " << (hps::kNumBanks - 1)
              << ")" << std::endl;
    return 1;
  }
  // Open file and seek to the end so that we can get the size.
  std::ifstream file(argv[2], std::ios::in | std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "Unable to open " << argv[1] << std::endl;
    return 1;
  }
  auto size = file.tellg();
  file.seekg(0);

  std::cout << "Downloading " << argv[2] << " (" << size;
  std::cout << " bytes) to bank " << bank << std::endl;
  // Assume downloading to start of bank.
  int written = hps->Download(bank, 0, file);
  file.close();
  if (written != size) {
    std::cerr << "Only wrote " << written << " bytes, write error!"
              << std::endl;
    return 1;
  }
  std::cout << "Successful download" << std::endl;
  return 0;
}

Command dl("dl", "dl <bank-id> <file> - Download file to hps.", download);

}  // namespace
