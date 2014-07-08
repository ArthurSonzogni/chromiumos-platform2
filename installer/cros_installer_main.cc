// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "installer/chromeos_install_config.h"
#include "installer/chromeos_legacy.h"
#include "installer/chromeos_postinst.h"
#include "installer/inst_util.h"

using std::string;

const char* usage = (
    "cros_installer:\n"
    "   --help\n"
    "   --debug\n"
    "   cros_installer postinst <install_dev> <mount_point> [ args ]\n"
    "     --bios [ secure | legacy | efi | uboot ]\n"
    "     --legacy\n"
    "     --postcommit\n");

int showHelp() {
  printf("%s", usage);
  return 1;
}

int main(int argc, char** argv) {
  struct option long_options[] = {
    {"bios", required_argument, NULL, 'b'},
    {"debug", no_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
    {"postcommit", no_argument, NULL, 'p'},
    {NULL, 0, NULL, 0},
  };

  // Unkown means we will attempt to autodetect later on.
  BiosType bios_type = kBiosTypeUnknown;

  while (true) {
    int option_index;
    int c = getopt_long(argc, argv, "h", long_options, &option_index);

    if (c == -1)
      break;

    switch (c) {
      case '?':
        // Unknown argument
        printf("\n");
        return showHelp();

      case 'h':
        // --help
        return showHelp();

      case 'b':
        // Bios type has been explicitly given, don't autodetect
        if (!StrToBiosType(optarg, &bios_type))
          return 1;
        break;

      case 'd':
        // I don't think this is used, but older update engines might
        // in some cases. So, it's present but ignored.
        break;

      case 'p':
        // This is an outdated argument. When we receive it, we just
        // exit with success right away.
        printf("Received --postcommit. This is a successful no-op.\n");
        return 0;

      default:
        printf("Unknown argument %d - switch and struct out of sync\n\n", c);
        return showHelp();
    }
  }

  if (argc - optind < 1) {
    printf("No command type present (postinst, etc)\n\n");
    return showHelp();
  }

  string command = argv[optind++];

  // Run postinstall behavior
  if (command == "postinst") {
    if (argc - optind != 2)
      return showHelp();

    string install_dir = argv[optind++];
    string install_dev = argv[optind++];

    int exit_code = 0;
    if (!RunPostInstall(install_dev, install_dir, bios_type, &exit_code)) {
      if (!exit_code)
        exit_code = 1;
    }

    return exit_code;
  }

  printf("Unknown command: '%s'\n\n", command.c_str());
  return showHelp();
}
