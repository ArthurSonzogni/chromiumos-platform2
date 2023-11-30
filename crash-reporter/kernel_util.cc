// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/kernel_util.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <re2/re2.h>

#include "crash-reporter/util.h"

using base::StringPrintf;

namespace {

constexpr char kDefaultKernelStackSignature[] =
    "kernel-UnspecifiedStackSignature";

// Byte length of maximum human readable portion of a kernel crash signature.
constexpr size_t kMaxHumanStringLength = 40;
// Time in seconds from the final kernel log message for a call stack
// to count towards the signature of the kcrash.
constexpr float kSignatureTimestampWindow = .200;
// Kernel log timestamp regular expression.
// Specify the multiline option so that ^ matches the start of lines, not just
// the start of the text. We have two variants, one that captures the timestamp
// and one that doesn't.
constexpr char kTimestampRegex[] = "(?m)^<.*>\\[\\s*(\\d+\\.\\d+)\\]";
constexpr char kTimestampNoCaptureRegex[] = "(?m)^<.*>\\[\\s*\\d+\\.\\d+\\]";

//
// These regular expressions enable to us capture the function name of
// the PC in a backtrace.
// The backtrace is obtained through dmesg or the kernel's preserved/kcrashmem
// feature.
//
// For ARM we see:
//   "<5>[   39.458982] PC is at write_breakme+0xd0/0x1b4" (arm32)
//   "<4>[  263.857834] pc : lkdtm_BUG+0xc/0x10" (arm64)
// For MIPS we see:
//   "<5>[ 3378.552000] epc   : 804010f0 lkdtm_do_action+0x68/0x3f8"
// For x86:
//   "<0>[   37.474699] EIP: [<790ed488>] write_breakme+0x80/0x108
//    SS:ESP 0068:e9dd3efc"
// For x86_64:
//   "<5>[ 1505.853254] RIP: 0010:[<ffffffff94fb0c27>] [<ffffffff94fb0c27>]
//   list_del_init+0x8/0x1b" (v4.10-)
//   "<4>[ 2358.194253] RIP: 0010:pick_task_fair+0x55/0x77" (v4.10+)
//
const char* const kPCFuncNameRegex[] = {
    nullptr, R"( (?:PC is at |pc : )([^\+\[ ]+).*)",
    R"( epc\s+:\s+\S+\s+([^\+ ]+).*)",  // MIPS has an exception
                                        // program counter
    R"( EIP: \[<.*>\] ([^\+ ]+).*)",    // X86 uses EIP for the
                                        // program counter
    R"( RIP: [[:xdigit:]]{4}:(?:\[<[[:xdigit:]]+>\] \[<[[:xdigit:]]+>\] )?)"
    R"(([^\+ ]+)\+0x.*)",  // X86_64 uses RIP
};

static_assert(std::size(kPCFuncNameRegex) == kernel_util::kArchCount,
              "Missing Arch PC func_name RegExp");

// Filter for boring functions. This should be a conservative list of functions
// that are never interesting since the magic signature code is more liberal
// when it comes to boring functions and there can be benefits of having both.
bool IsBoringFunction(const std::string& function) {
  static const std::vector<std::string>* kBoringFunctions =
      new std::vector<std::string>({
          "__flush_work",
          "__mutex_lock",
          "__mutex_lock_common",
          "__mutex_lock_slowpath",
          "__switch_to",
          "__schedule",
          "__wait_on_bit",
          "__wait_on_buffer",
          "bit_wait_io",
          "down_read",
          "down_write",
          "down_write_killable",
          "dump_backtrace",
          "dump_cpu_task",
          "dump_stack",
          "dump_stack_lvl",
          "flush_work",
          "io_schedule",
          "kthread_flush_work",
          "mutex_lock",
          "out_of_line_wait_on_bit",
          "panic",
          "rcu_dump_cpu_stacks",
          "rwsem_down_read_slowpath",
          "rwsem_down_write_slowpath",
          "sched_show_task",
          "schedule",
          "schedule_hrtimeout_range",
          "schedule_hrtimeout_range_clock",
          "schedule_preempt_disabled",
          "schedule_timeout",
          "schedule_timeout_uninterruptible",
          "show_stack",
          "usleep_range_state",
          "wait_for_completion",
      });

  for (const auto& to_match : *kBoringFunctions) {
    if (to_match == function) {
      return true;
    }
  }

  return false;
}

// Find the most relevant stack trace in the log and sets `hash`,
// `stack_fn`, and `crash_tag` appropriately.
//
// Outputs:
// `hash`: The hash of the names of all the functions in the stack.
// `stack_fn`: The name of the most relevant function on that stack.
// `crash_tag`: An optional tag that indicates if this is a
//              special kind of stack (like a hang). Set to the empty
//              string if this is not a special kind of crash.
//
// This function will return true if it's confident in the human readable
// string. If it's not confident, it will still set the return values to
// something reasonable, but it means we should look elsewhere (like for a
// panic message) for a human readable string if we can find it.
bool ProcessStackTrace(re2::StringPiece kernel_dump,
                       kernel_util::ArchKind arch,
                       unsigned* hash,
                       std::string* stack_fn,
                       std::string* crash_tag) {
  RE2 line_re("(.+)");

  RE2::Options opt;
  opt.set_case_sensitive(false);
  RE2 warning_start_re(std::string(kTimestampNoCaptureRegex) + " WARNING: ");
  RE2 warning_end_re(std::string(kTimestampNoCaptureRegex) +
                     " ---\\[ end trace [[:xdigit:]]+ \\]---");
  RE2 hard_lockup_re(std::string(kTimestampNoCaptureRegex) +
                     " Watchdog detected hard LOCKUP");
  RE2 soft_lockup_re(std::string(kTimestampNoCaptureRegex) +
                     " watchdog: BUG: soft lockup");
  RE2 hung_task_re(std::string(kTimestampNoCaptureRegex) +
                   " INFO: task .*:\\d+ blocked for more than");
  RE2 stack_trace_start_re(
      std::string(kTimestampNoCaptureRegex) + " (Call Trace|Backtrace):$", opt);

  // Match lines such as the following and grab out "function_name".
  // The ? may or may not be present.
  //
  // For ARM:
  // <4>[ 3498.731164] [<c0057220>] ? (function_name+0x20/0x2c) from
  // [<c018062c>] (foo_bar+0xdc/0x1bc) (arm32 older)
  // <4>[  263.956936]  lkdtm_do_action+0x24/0x40 (arm64 / arm32 newer)
  //
  // For MIPS:
  // <5>[ 3378.656000] [<804010f0>] lkdtm_do_action+0x68/0x3f8
  //
  // For X86:
  // <4>[ 6066.849504]  [<7937bcee>] ? function_name+0x66/0x6c
  // <4>[ 2358.194379]  __schedule+0x83f/0xf92 (newer) like arm64 above
  //
  RE2 stack_entry_re(
      std::string(kTimestampRegex) +
      R"(\s+(?:\[<[[:xdigit:]]+>\])?)"  // Matches "  [<7937bcee>]" (if any)
      R"(([\s?(]+))"                    // Matches " ? (" (ARM) or " ? " (X86)
      R"(([^\+ )]+))");                 // Matches until delimiter reached
  std::string line;
  std::string hashable;
  std::string uncertain_hashable;
  float stack_timestamp = 0;
  bool found_the_stack = false;
  bool want_next_stack = false;
  bool in_warning = false;

  // Use the correct regex for this architecture.
  if (kPCFuncNameRegex[arch] == nullptr) {
    LOG(WARNING) << "PC func_name RegExp is not defined for this architecture";
    return false;
  }
  RE2 cpureg_fn_re(std::string(kTimestampRegex) + kPCFuncNameRegex[arch]);
  std::string cpureg_fn;
  float cpureg_timestamp = 0;

  stack_fn->clear();
  crash_tag->clear();
  *hash = 0;

  // Find the last stack trace, unless we see an indication that there was a
  // hang of some sort. In those cases we pick the first stack trace after
  // we see the hang message since the kernel always tries to trace the
  // hung task first.
  while (RE2::FindAndConsume(&kernel_dump, line_re, &line)) {
    std::string certainty;
    std::string function_name;

    // While we're in a warning we eat lines until we get out of the warning.
    // Warnings are collected by the warning collector--we never want them
    // here in the kernel collector.
    if (in_warning) {
      if (RE2::PartialMatch(line, warning_end_re)) {
        in_warning = false;
      }
    } else {
      in_warning = RE2::PartialMatch(line, warning_start_re);
    }
    if (in_warning) {
      continue;
    }

    // After we've skipped warnings, always capture the function from any
    // CPU registers that we see. This is often going to be the same function
    // name we capture below (AKA stack_fn).
    if (RE2::PartialMatch(line, cpureg_fn_re, &cpureg_timestamp, &cpureg_fn)) {
      if (IsBoringFunction(cpureg_fn)) {
        cpureg_fn.clear();
        cpureg_timestamp = 0;
      }
    }

    if (RE2::PartialMatch(line, hard_lockup_re)) {
      want_next_stack = true;
      *crash_tag = "(HARDLOCKUP)-";
    } else if (RE2::PartialMatch(line, soft_lockup_re)) {
      want_next_stack = true;
      *crash_tag = "(SOFTLOCKUP)-";
    } else if (RE2::PartialMatch(line, hung_task_re)) {
      want_next_stack = true;
      *crash_tag = "(HANG)-";
    } else if (RE2::PartialMatch(line, stack_trace_start_re)) {
      // We set `found_the_stack` true once we've started parsing the 1st stack
      // after a watchdog message. Break as soon as we see yet another stack.
      if (found_the_stack) {
        break;
      }
      hashable.clear();
      uncertain_hashable.clear();
      stack_fn->clear();
      found_the_stack = want_next_stack;
    } else if (RE2::PartialMatch(line, stack_entry_re, &stack_timestamp,
                                 &certainty, &function_name)) {
      bool is_certain = certainty.find('?') == std::string::npos;

      // Keep track of two hashables, one that doesn't include include any
      // uncertain (prefixed by '?') frames and ones that includes all frames.
      // We only use the uncertain hashable if there are no certain frames.
      if (!uncertain_hashable.empty())
        uncertain_hashable.append("|");
      uncertain_hashable.append(function_name);
      if (!is_certain)
        continue;
      if (!hashable.empty())
        hashable.append("|");
      hashable.append(function_name);

      // Store the first non-ignored function since that's a good candidate
      // for the "human readable" part of the signature.
      if (stack_fn->empty() && !IsBoringFunction(function_name)) {
        *stack_fn = function_name;
      }
    }
  }

  // If the hashable is empty (meaning all frames are uncertain, for whatever
  // reason) use the uncertain hashable, as it cannot be any worse.
  if (hashable.empty()) {
    hashable = uncertain_hashable;
  }

  *hash = util::HashString(hashable);

  // We'll claim that we have a good result if either:
  // - We have a tag, which means we recognized a hang.
  // - We got a PC from CPU Registers that's has a timestamp that was recent.
  //   This covers the pattern of:
  //     __show_regs(regs);
  //     panic("message");
  //   Where the "regs" has the actual failing PC (and thus is extremely
  //   relevant). Note that panic() never prints CPU registers.
  if (!crash_tag->empty()) {
    return true;
  } else if (!cpureg_fn.empty() &&
             stack_timestamp - cpureg_timestamp < kSignatureTimestampWindow) {
    *stack_fn = cpureg_fn;
    return true;
  }

  return false;
}

bool FindPanicMessage(re2::StringPiece kernel_dump,
                      std::string* panic_message) {
  // Match lines such as the following and grab out "Fatal exception"
  // <0>[  342.841135] Kernel panic - not syncing: Fatal exception
  RE2 kernel_panic_re(std::string(kTimestampRegex) +
                      " Kernel panic[^\\:]*\\:\\s*(.*)");
  float timestamp = 0;
  while (RE2::FindAndConsume(&kernel_dump, kernel_panic_re, &timestamp,
                             panic_message)) {
  }
  if (timestamp == 0) {
    LOG(INFO) << "Found no panic message";
    return false;
  }
  return true;
}

}  // namespace

namespace kernel_util {

const char kKernelExecName[] = "kernel";
const char kHypervisorExecName[] = "hypervisor";

bool IsHypervisorCrash(const std::string& kernel_dump) {
  RE2 hypervisor_re("Linux version [0-9.]+-manatee");
  return RE2::PartialMatch(kernel_dump, hypervisor_re);
}

ArchKind GetCompilerArch() {
#if defined(COMPILER_GCC) && defined(ARCH_CPU_ARM_FAMILY)
  return kArchArm;
#elif defined(COMPILER_GCC) && defined(ARCH_CPU_MIPS_FAMILY)
  return kArchMips;
#elif defined(COMPILER_GCC) && defined(ARCH_CPU_X86_64)
  return kArchX86_64;
#elif defined(COMPILER_GCC) && defined(ARCH_CPU_X86_FAMILY)
  return kArchX86;
#else
  return kArchUnknown;
#endif
}

std::string ComputeKernelStackSignature(const std::string& kernel_dump,
                                        ArchKind arch) {
  unsigned stack_hash = 0;
  std::string crash_tag;
  std::string human_string;

  if (!ProcessStackTrace(kernel_dump, arch, &stack_hash, &human_string,
                         &crash_tag)) {
    FindPanicMessage(kernel_dump, &human_string);
  }

  if (human_string.empty() && stack_hash == 0) {
    LOG(WARNING) << "Cannot find a stack or a human readable string";
    return kDefaultKernelStackSignature;
  }

  human_string = human_string.substr(0, kMaxHumanStringLength);
  return StringPrintf("%s-%s%s-%08X", kKernelExecName, crash_tag.c_str(),
                      human_string.c_str(), stack_hash);
}

std::string BiosCrashSignature(const std::string& dump) {
  const char* type = "";

  if (RE2::PartialMatch(dump, RE2("PANIC in EL3")))
    type = "PANIC";
  else if (RE2::PartialMatch(dump, RE2("Unhandled Exception in EL3")))
    type = "EXCPT";
  else if (RE2::PartialMatch(dump, RE2("Unhandled Interrupt Exception in")))
    type = "INTR";

  std::string elr;
  RE2::PartialMatch(dump, RE2("x30 =\\s+(0x[0-9a-fA-F]+)"), &elr);

  return StringPrintf("bios-(%s)-%s", type, elr.c_str());
}

std::string ComputeNoCErrorSignature(const std::string& dump) {
  RE2 line_re("(.+)");
  re2::StringPiece dump_piece = dump;

  // Match lines such as the following and grab out the type of NoC (MMSS)
  // and the register contents
  //
  // QTISECLIB [1727120e379]MMSS_NOC ERROR: ERRLOG0_LOW = 0x00000105
  //
  RE2 noc_entry_re(R"(QTISECLIB \[[[:xdigit:]]+\]([a-zA-Z]+)_NOC ERROR: )"
                   R"(ERRLOG[0-9]_(?:(LOW|HIGH)) = ([[:xdigit:]]+))");
  std::string line;
  std::string hashable;
  std::string noc_name;
  std::string first_noc;
  std::string regval;

  // Look at each line of the bios log for the NOC errors and compute a hash
  // of all the registers
  while (RE2::FindAndConsume(&dump_piece, line_re, &line)) {
    if (RE2::PartialMatch(line, noc_entry_re, &noc_name, &regval)) {
      if (!hashable.empty())
        hashable.append("|");
      if (first_noc.empty())
        first_noc = noc_name;
      hashable.append(noc_name);
      hashable.append("|");
      hashable.append(regval);
    }
  }

  unsigned hash = util::HashString(hashable);

  return StringPrintf("%s-(NOC-Error)-%s-%08X", kKernelExecName,
                      first_noc.c_str(), hash);
}

// Watchdog reboots leave no stack trace. Generate a poor man's signature out
// of the last log line instead (minus the timestamp ended by ']').
std::string WatchdogSignature(const std::string& console_ramoops,
                              const std::string& watchdogRebootReason) {
  std::string_view line(console_ramoops);
  constexpr char kTimestampEnd[] = "] ";
  size_t timestamp_end_pos = line.rfind(kTimestampEnd);
  if (timestamp_end_pos != std::string_view::npos) {
    line = line.substr(timestamp_end_pos + strlen(kTimestampEnd));
  }
  size_t newline_pos = line.find("\n");
  size_t end = (newline_pos == std::string_view::npos
                    ? std::string_view::npos
                    : std::min(newline_pos, kMaxHumanStringLength));
  return StringPrintf(
      "%s%s-%s-%08X", kKernelExecName, watchdogRebootReason.c_str(),
      std::string(line.substr(0, end)).c_str(), util::HashString(line));
}

bool ExtractHypervisorLog(std::string& console_ramoops,
                          std::string& hypervisor_log) {
  RE2 hypervisor_log_re("(?s)(\\n-*\\[ hypervisor log \\]-*\\n)(.*)$");
  re2::StringPiece header;
  if (RE2::PartialMatch(console_ramoops, hypervisor_log_re, &header,
                        &hypervisor_log)) {
    console_ramoops.resize(console_ramoops.size() - hypervisor_log.size() -
                           header.size());
    return true;
  }
  return false;
}

}  // namespace kernel_util
