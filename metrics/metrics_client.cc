// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <cstdlib>

#include "metrics/metrics_library.h"

namespace {

enum Mode {
  kModeSendSample,
  kModeSendEnumSample,
  kModeSendSparseSample,
  kModeSendUserAction,
  kModeSendCrosEvent,
  kModeHasConsent,
  kModeIsGuestMode,
  kModeShowConsentId,
  kModeCreateConsent,
  kModeDeleteConsent,
  kModeReplayFile,
};

void ShowUsage() {
  fprintf(
      stderr,
      "Usage:  metrics_client [-W <file>] [-n <num_samples>] [-t] name sample "
      "min max nbuckets\n"
      "        metrics_client [-W <file>] [-n <num_samples>] -e   name sample "
      "max\n"
      "        metrics_client [-W <file>] [-n <num_samples>] -s   name sample\n"
      "        metrics_client [-W <file>] [-n <num_samples>] -v   event\n"
      "        metrics_client [-W <file>] [-n <num_samples>] -u action\n"
      "        metrics_client [-W <file>] -R <file>\n"
      "        metrics_client [-cCDg]\n"
      "\n"
      "  default: send an integer-valued histogram sample\n"
      "           |min| > 0, |min| <= sample < |max|\n"
      "  -C: Create consent file such that -c will return 0.\n"
      "  -D: Delete consent file such that -c will return 1.\n"
      "  -R <file>: Replay events from a file and truncate it.\n"
      "  -W <file>: Write events to a file; append to it if it exists.\n"
      "  -c: return exit status 0 if user consents to stats, 1 otherwise,\n"
      "      in guest mode always return 1\n"
      "  -e: send linear/enumeration histogram data\n"
      "  -g: return exit status 0 if machine in guest mode, 1 otherwise\n"
      "  -n <num_samples>: Sends |num_samples| identical samples\n"
      // The -i flag prints the client ID, if it exists and is valid.
      // It is not advertised here because it is deprecated and for internal
      // use only (at least by the log tool in debugd).
      "  -s: send a sparse histogram sample\n"
      "  -t: convert sample from double seconds to int milliseconds\n"
      "  -u: send a user action\n"
      "  -v: send a Platform.CrOSEvent enum histogram sample\n");
  exit(1);
}

int ParseInt(const char* arg) {
  char* endptr;
  int value = strtol(arg, &endptr, 0);
  if (*endptr != '\0') {
    fprintf(stderr, "metrics client: bad integer \"%s\"\n", arg);
    ShowUsage();
  }
  return value;
}

double ParseDouble(const char* arg) {
  char* endptr;
  double value = strtod(arg, &endptr);
  if (*endptr != '\0') {
    fprintf(stderr, "metrics client: bad double \"%s\"\n", arg);
    ShowUsage();
  }
  return value;
}

int SendStats(char* argv[],
              int name_index,
              enum Mode mode,
              bool secs_to_msecs,
              const char* output_file,
              int num_samples) {
  const char* name = argv[name_index];
  int sample;
  if (secs_to_msecs) {
    sample = static_cast<int>(ParseDouble(argv[name_index + 1]) * 1000.0);
  } else {
    sample = ParseInt(argv[name_index + 1]);
  }

  MetricsLibrary metrics_lib;
  if (output_file) {
    metrics_lib.SetOutputFile(output_file);
  }
  if (mode == kModeSendSparseSample) {
    metrics_lib.SendRepeatedSparseToUMA(name, sample, num_samples);
  } else if (mode == kModeSendEnumSample) {
    int exclusive_max = ParseInt(argv[name_index + 2]);
    metrics_lib.SendRepeatedEnumToUMA(name, sample, exclusive_max, num_samples);
  } else {
    int min = ParseInt(argv[name_index + 2]);
    int max = ParseInt(argv[name_index + 3]);
    int nbuckets = ParseInt(argv[name_index + 4]);
    metrics_lib.SendRepeatedToUMA(name, sample, min, max, nbuckets,
                                  num_samples);
  }
  return 0;
}

int SendUserAction(char* argv[], int action_index, int num_samples) {
  const char* action = argv[action_index];
  MetricsLibrary metrics_lib;
  metrics_lib.SendRepeatedUserActionToUMA(action, num_samples);
  return 0;
}

int SendCrosEvent(char* argv[], int action_index, int num_samples) {
  const char* event = argv[action_index];
  bool result;
  MetricsLibrary metrics_lib;
  result = metrics_lib.SendRepeatedCrosEventToUMA(event, num_samples);
  if (!result) {
    fprintf(stderr, "metrics_client: could not send event %s\n", event);
    return 1;
  }
  return 0;
}

int CreateConsent() {
  MetricsLibrary metrics_lib;
  return metrics_lib.EnableMetrics() ? 0 : 1;
}

int DeleteConsent() {
  MetricsLibrary metrics_lib;
  return metrics_lib.DisableMetrics() ? 0 : 1;
}

int HasConsent() {
  MetricsLibrary metrics_lib;
  return metrics_lib.AreMetricsEnabled() ? 0 : 1;
}

int IsGuestMode() {
  MetricsLibrary metrics_lib;
  return metrics_lib.IsGuestMode() ? 0 : 1;
}

int ShowConsentId() {
  MetricsLibrary metrics_lib;
  std::string id;
  if (metrics_lib.ConsentId(&id) == false) {
    fprintf(stderr, "error: consent not given\n");
    return 1;
  }
  printf("%s\n", id.c_str());
  return 0;
}

int ReplayFile(const char* input_file, const char* output_file) {
  MetricsLibrary metrics_lib;
  if (output_file) {
    metrics_lib.SetOutputFile(output_file);
  }
  return metrics_lib.Replay(input_file) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  enum Mode mode = kModeSendSample;
  bool secs_to_msecs = false;
  const char* output_file = nullptr;
  const char* input_file = nullptr;
  const char* num_samples_cstr = nullptr;

  // Parse arguments
  int flag;
  while ((flag = getopt(argc, argv, "CDR:W:cegin:stuv")) != -1) {
    switch (flag) {
      case 'C':
        mode = kModeCreateConsent;
        break;
      case 'D':
        mode = kModeDeleteConsent;
        break;
      case 'R':
        mode = kModeReplayFile;
        input_file = optarg;
        break;
      case 'W':
        output_file = optarg;
        break;
      case 'c':
        mode = kModeHasConsent;
        break;
      case 'e':
        mode = kModeSendEnumSample;
        break;
      case 'g':
        mode = kModeIsGuestMode;
        break;
      case 'i':
        // This flag is slated for removal.
        // See comment in ShowUsage().
        mode = kModeShowConsentId;
        break;
      case 'n':
        num_samples_cstr = optarg;
        break;
      case 's':
        mode = kModeSendSparseSample;
        break;
      case 't':
        secs_to_msecs = true;
        break;
      case 'u':
        mode = kModeSendUserAction;
        break;
      case 'v':
        mode = kModeSendCrosEvent;
        break;
      default:
        ShowUsage();
        break;
    }
  }
  int arg_index = optind;

  int num_samples = 1;
  if (num_samples_cstr) {
    num_samples = ParseInt(num_samples_cstr);
    if (num_samples <= 0) {
      fprintf(stderr, "metrics client: bad num_samples \"%s\"\n",
              num_samples_cstr);
      ShowUsage();
    }
  }

  int expected_args = 0;
  if (mode == kModeSendSample)
    expected_args = 5;
  else if (mode == kModeSendEnumSample)
    expected_args = 3;
  else if (mode == kModeSendSparseSample)
    expected_args = 2;
  else if (mode == kModeSendUserAction)
    expected_args = 1;
  else if (mode == kModeSendCrosEvent)
    expected_args = 1;

  if ((arg_index + expected_args) != argc) {
    ShowUsage();
  }

  switch (mode) {
    case kModeSendSample:
    case kModeSendEnumSample:
    case kModeSendSparseSample:
      if ((mode != kModeSendSample) && secs_to_msecs) {
        ShowUsage();
      }
      return SendStats(argv, arg_index, mode, secs_to_msecs, output_file,
                       num_samples);
    case kModeSendUserAction:
      return SendUserAction(argv, arg_index, num_samples);
    case kModeSendCrosEvent:
      return SendCrosEvent(argv, arg_index, num_samples);
    case kModeCreateConsent:
      return CreateConsent();
    case kModeDeleteConsent:
      return DeleteConsent();
    case kModeHasConsent:
      return HasConsent();
    case kModeIsGuestMode:
      return IsGuestMode();
    case kModeShowConsentId:
      return ShowConsentId();
    case kModeReplayFile:
      return ReplayFile(input_file, output_file);
    default:
      ShowUsage();
      return 0;
  }
}
