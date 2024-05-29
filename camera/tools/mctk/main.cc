/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <optional>
#include <string>

#include "tools/mctk/debug.h"
#include "tools/mctk/find_mcdev_file.h"
#include "tools/mctk/mcdev.h"
#include "tools/mctk/merge.h"
#include "tools/mctk/remap.h"
#include "tools/mctk/routing.h"
#include "tools/mctk/yaml_tree.h"

extern int mctk_verbosity;

namespace {

void PrintUsage(char* progname) {
  fprintf(stderr, "\n");

  fprintf(stderr,
          "Example usage: %s --load-device /dev/media0 --dump-yaml "
          "/proc/self/fd/1\n",
          progname);
  fprintf(stderr,
          "Example usage: %s --load-device /dev/media0 --reset-links "
          "--merge-yaml config.yaml\n",
          progname);
  fprintf(stderr,
          "Example usage: %s --load-device /dev/media0 --reset-links "
          "--auto-route\n",
          progname);

  fprintf(
      stderr,
      "\n"
      "Options, executed in the order they are passed in:\n"
      "\n"
      "  -h, --help                        Print this help message.\n"
      "\n"
      "  -v, --verbose                     Increase verbosity.\n"
      "\n"
      "  -d, --load-device <devfile>       Work on a real /dev/mediaX device.\n"
      "                                    Changes propagate to the kernel.\n"
      "      --load-by-businfo <bus_info>  Similar to --load-device, picking\n"
      "                                    a /dev/media* device by bus_info.\n"
      "\n"
      "      --load-yaml   <yamlfile>      Work on a virtual media-ctl read\n"
      "                                    from a YAML file.\n"
      "      --dump-yaml   <yamlfile>      Dump active model to a YAML file.\n"
      "      --merge-yaml  <yamlfile>      Merge settings from a YAML file.\n"
      "\n"
      "  -r, --reset-links                 Disable all links in active model.\n"
      "\n"
      "Unfinished options:\n"
      "      --auto-route                  Guess a route from each sensor to\n"
      "                                    a /dev/videoX device.\n"
      "\n");
}

std::unique_ptr<V4lMcDev> OpenMcDevKernel(std::string path) {
  int fd_mc = open(path.c_str(), O_RDWR);
  if (fd_mc < 0) {
    MCTK_PERROR("Failed to open media controller device " + path);
    return nullptr;
  }

  std::unique_ptr<V4lMcDev> mcdev = V4lMcDev::CreateFromKernel(fd_mc);
  if (!mcdev) {
    close(fd_mc);
    MCTK_ERR("CreateFromKernel() for MC device failed. Aborting.");
    return nullptr;
  }

  /* NOTE: mcdev now owns the fd, and will close it in its destructor. */
  return mcdev;
}

}  // namespace

int main(int argc, char** argv) {
  std::unique_ptr<V4lMcDev> mcdev;
  int option_index = 0;
  int opt;

  static struct option long_options[] = {
      {"help", 0, 0, 'h'},

      {"verbose", 0, 0, 'v'},

      {"load-device", 1, 0, 'd'},  {"load-by-businfo", 1, 0, 10005},

      {"load-yaml", 1, 0, 10001},  {"dump-yaml", 1, 0, 10002},
      {"merge-yaml", 1, 0, 10003},

      {"reset-links", 0, 0, 'r'},

      {"auto-route", 0, 0, 10004}, {NULL, 0, NULL, 0}};

  if (argc < 2) {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  /* Process arguments one by one, like a command list */
  while ((opt = getopt_long(argc, argv, "hvd:r", long_options,
                            &option_index)) != -1) {
    switch (opt) {
      default:
      case 'h':
        PrintUsage(argv[0]);
        break;

      case 'v':
        ++mctk_verbosity;
        break;

      case 'd': /* --load-device */ {
        if (mcdev) {
          MCTK_ERR(
              "A media-ctl model is already loaded - cannot load another. "
              "Aborting.");
          return EXIT_FAILURE;
        }

        mcdev = OpenMcDevKernel(optarg);
        if (!mcdev)
          return EXIT_FAILURE;

        break;
      }

      case 10005: /* --load-by-businfo */ {
        if (mcdev) {
          MCTK_ERR(
              "A media-ctl model is already loaded - cannot load another. "
              "Aborting.");
          return EXIT_FAILURE;
        }

        std::optional<std::string> path = MctkFindMcDevByBusInfo(optarg);
        if (!path) {
          MCTK_ERR("No device with given bus_info found. Aborting.");
          return EXIT_FAILURE;
        }

        mcdev = OpenMcDevKernel(path.value());
        if (!mcdev)
          return EXIT_FAILURE;

        break;
      }

      case 'r': /* --reset-links */ {
        if (!mcdev) {
          MCTK_ERR("No media-ctl model loaded. Cannot reset links.");
          return EXIT_FAILURE;
        }

        MCTK_VERBOSE("Resetting links.");
        mcdev->ResetLinks();

        break;
      }

      case 10001: /* --load-yaml */ {
        if (mcdev) {
          MCTK_ERR(
              "A media-ctl model is already loaded - cannot load another. "
              "Aborting.");
          return EXIT_FAILURE;
        }

        FILE* f = fopen(optarg, "r");
        if (!f) {
          MCTK_PERROR("Failed to open YAML file for MC device");
          return EXIT_FAILURE;
        }

        std::unique_ptr<YamlNode> root = YamlNode::FromFile(*f);
        fclose(f);
        if (!root) {
          MCTK_ERR("YamlNode::FromFile() for MC device failed. Aborting.");
          return EXIT_FAILURE;
        }

        // Debug: Print parsed YAML tree
        // root->ToFile(*stdout);

        mcdev = V4lMcDev::CreateFromYamlNode((*root)["media_ctl"]);
        if (!mcdev) {
          MCTK_ERR("CreateFromYamlNode() for MC device failed. Aborting.");
          return EXIT_FAILURE;
        }

        break;
      }

      case 10002: /* --dump-yaml */ {
        if (!mcdev) {
          MCTK_ERR("No media-ctl model loaded. Cannot dump to YAML.");
          return EXIT_FAILURE;
        }

        FILE* f = fopen(optarg, "w");
        if (!f) {
          MCTK_PERROR("Failed to open YAML file for dump");
          return EXIT_FAILURE;
        }

        mcdev->ToYamlFile(*f);
        fclose(f);

        break;
      }

      case 10003: /* --merge-yaml */ {
        if (!mcdev) {
          MCTK_ERR("No media-ctl model loaded. Nothing to merge into.");
          return EXIT_FAILURE;
        }

        FILE* f = fopen(optarg, "r");
        if (!f) {
          MCTK_PERROR("Failed to open YAML file for merge source");
          return EXIT_FAILURE;
        }

        std::unique_ptr<YamlNode> root = YamlNode::FromFile(*f);
        fclose(f);
        if (!root) {
          MCTK_ERR("YamlNode::FromFile() for merge source failed. Aborting.");
          return EXIT_FAILURE;
        }

        auto remap =
            V4lMcRemap::CreateFromYamlNode((*root)["remap_entity_by_name"]);
        if (!remap) {
          MCTK_ERR(
              "CreateFromYamlNode() for remap failed. No entity remapping.");
          return EXIT_FAILURE;
        }

        auto merge_source = V4lMcDev::CreateFromYamlNode((*root)["media_ctl"]);
        if (!merge_source) {
          MCTK_ERR("CreateFromYamlNode() for merge source failed. Aborting.");
          return EXIT_FAILURE;
        }

        if (!V4lMcMergeMcDev(*mcdev, *merge_source,
                             remap ? remap.get() : nullptr)) {
          MCTK_ERR("V4lMcMergeMcDev() failed. Aborting.");
          return EXIT_FAILURE;
        }

        break;
      }

      case 10004: /* --auto-route */ {
        if (!mcdev) {
          MCTK_ERR("No media-ctl model loaded. Nothing to autoroute.");
          return EXIT_FAILURE;
        }

        MCTK_VERBOSE("Autorouting sensors.");
        V4lMcRouteSensors(*mcdev);

        break;
      }
    } /* switch */
  }   /* while */

  return EXIT_SUCCESS;
}
