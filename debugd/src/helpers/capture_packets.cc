// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// packet capture helper.  This initiates packet capture on a device
// and stores the output pcap file to the specified destination.

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/capability.h>
#include <sys/types.h>

#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece.h>
#include <chromeos/libminijail.h>
#include <pcap.h>

#define RECEIVE_PACKET_SIZE 2048
#define PACKET_TIMEOUT_MS 1000

namespace {
// Path of the seccomp filter to apply.
constexpr char kSeccompFilterPath[] =
    "/usr/share/policy/capture-packets.policy";

int perform_capture(base::StringPiece device,
                    base::StringPiece output_file,
                    base::StringPiece max_size) {
  // Limit the capabilities of the process to required ones.
  const cap_value_t requiredCaps[] = {CAP_SYS_ADMIN, CAP_SETUID, CAP_SETGID,
                                      CAP_NET_RAW};
  cap_t caps = cap_get_proc();
  if (cap_clear(caps) ||
      cap_set_flag(caps, CAP_EFFECTIVE, base::size(requiredCaps), requiredCaps,
                   CAP_SET) ||
      cap_set_flag(caps, CAP_PERMITTED, base::size(requiredCaps), requiredCaps,
                   CAP_SET) ||
      cap_set_flag(caps, CAP_INHERITABLE, base::size(requiredCaps),
                   requiredCaps, CAP_SET)) {
    fprintf(
        stderr,
        "Can't clear capabilities and set flags for required capabilities.\n");
    return 1;
  }
  if (cap_set_proc(caps)) {
    fprintf(stderr, "Can't set capabilities.\n");
    return 1;
  }

  char buf[RECEIVE_PACKET_SIZE];
  const int promiscuous = 0;
  char errbuf[PCAP_ERRBUF_SIZE];
  pcap_t* pcap = pcap_open_live(device.data(), sizeof(buf), promiscuous,
                                PACKET_TIMEOUT_MS, errbuf);
  if (pcap == nullptr) {
    fprintf(stderr, "Could not open capture handle.\n");
    return -1;
  }

  int output_file_descriptor;
  if (!base::StringToInt(output_file, &output_file_descriptor)) {
    fprintf(stderr,
            "Can't parse file descriptor value from the output file argument. "
            "Make sure you pass a valid file descriptor value.\n");
    return 1;
  }
  FILE* output_fp = fdopen(output_file_descriptor, "a");
  if (output_fp == nullptr) {
    fprintf(stderr,
            "File pointer to the output file can't be created from given file "
            "descriptor.\n");
    return 1;
  }
  pcap_dumper_t* dumper = pcap_dump_fopen(pcap, output_fp);
  if (dumper == nullptr) {
    fprintf(stderr, "Could not open dump file.\n");
    return -1;
  }

  u_int64_t max_size_parsed;
  if (!base::StringToUint64(max_size, &max_size_parsed)) {
    fprintf(
        stderr,
        "Can't parse max-size argument. Make sure you pass unsigned int!\n");
    return 1;
  }
  // max_size argument is given in MiB. Convert max_size from MiB to bytes.
  int mib_to_byte_conversion = 1048576;
  u_int64_t max_capture_size = max_size_parsed * mib_to_byte_conversion;
  u_int64_t total_captured_size = 0;

  // Now that we have all our handles open, drop privileges.
  struct minijail* j = minijail_new();
  minijail_namespace_vfs(j);
  // Use seccomp filter.
  minijail_use_seccomp_filter(j);
  minijail_parse_seccomp_filters(j, kSeccompFilterPath);
  minijail_change_user(j, "debugd");
  minijail_change_group(j, "debugd");
  minijail_no_new_privs(j);
  minijail_enter(j);

  int packet_count = 0;
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGTERM);
  sigaddset(&sigset, SIGINT);
  sigprocmask(SIG_BLOCK, &sigset, nullptr);
  while (sigpending(&sigset) == 0) {
    if (sigismember(&sigset, SIGTERM) || sigismember(&sigset, SIGINT)) {
      break;
    }
    struct pcap_pkthdr header;
    const unsigned char* packet = pcap_next(pcap, &header);
    if (packet == nullptr || header.len == 0) {
      continue;
    }
    ++packet_count;
    total_captured_size += header.caplen;
    pcap_dump(reinterpret_cast<u_char*>(dumper), &header, packet);
    if (max_capture_size && total_captured_size >= max_capture_size) {
      fprintf(
          stderr,
          "Reached capture file size limit! Stopping packet capture now.\n");
      break;
    }
  }

  pcap_close(pcap);
  pcap_dump_close(dumper);

  printf("Exiting after %d captured packets\n", packet_count);

  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <device> <output_file> <max_size>\n", argv[0]);
    return 1;
  }

  return perform_capture(argv[1], argv[2], argv[3]);
}
