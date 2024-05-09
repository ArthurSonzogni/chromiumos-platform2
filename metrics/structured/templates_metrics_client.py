# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Templates for generating the metrics_client parser for --structured."""

FILE_TEMPLATE = """\
// Generated from gen_metrics_client_events.py. DO NOT EDIT!
// source: structured.xml

#include "metrics/structured/metrics_client_structured_events.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <string_view>

#include "metrics/metrics_client_util.h"
#include "metrics/structured/structured_events.h"

namespace metrics_client {{
int SendStructuredMetric(int argc, const char* const argv[], int current_arg,
                         FILE* err) {{
  if (argc == current_arg) {{
    fprintf(err, "metrics client: missing project name\\n");
    ShowUsage(err);
    return EXIT_FAILURE;
  }}
  std::string_view project = argv[current_arg];
  ++current_arg;
  if (argc == current_arg) {{
    fprintf(err, "metrics client: missing event name\\n");
    ShowUsage(err);
    return EXIT_FAILURE;
  }}
  std::string_view event = argv[current_arg];
  ++current_arg;

{project_code}

  fprintf(err, "metrics client: Unknown project %s\\n", project.data());
  ShowUsage(err);
  return EXIT_FAILURE;
}}
}}  // namespace metrics_client
"""

PROJECT_TEMPLATE = """\
  if (project == "{project.raw_name}") {{
{event_code}

    fprintf(err,
            "metrics client: Unknown event %s for project {project.raw_name}\\n",
            event.data());
    ShowUsage(err);
    return EXIT_FAILURE;
  }}\
"""

EVENT_TEMPLATE = """\
    if (event == "{event.raw_name}") {{
      metrics::structured::events::{project.namespace}::{event.name} event;
{pre_metric_code}

      while (current_arg < argc) {{
        std::string_view arg = argv[current_arg];
        if (arg.starts_with("--")) {{
          arg.remove_prefix(2);
        }} else if (arg.starts_with("-")) {{
          arg.remove_prefix(1);
        }} else {{
          fprintf(err, "metrics client: Unexpected arg %s\\n", arg.data());
          ShowUsage(err);
          return EXIT_FAILURE;
        }}
        auto break_point = arg.find('=');
        std::string_view metric_name;
        std::string_view metric_value_string;
        if (break_point == std::string_view::npos) {{
          if (current_arg + 1 == argc) {{
            fprintf(err, "metrics client: argument %s has no value\\n",
                    argv[current_arg]);
            ShowUsage(err);
            return EXIT_FAILURE;
          }}
          metric_name = arg;
          metric_value_string = argv[current_arg + 1];
          current_arg += 2;
        }} else {{
          metric_name = arg.substr(0, break_point);
          metric_value_string = arg.substr(break_point + 1);
          ++current_arg;
        }}

        if (false) {{
          // dummy block so that the generated code can use 'else if' for
          // every metric.
        }}
{metric_code}\
        else {{
          // Note: metric_name.data() may not be nul-terminated. Create a
          // std::string for %s usage.
          std::string metric_name_str(metric_name);
          fprintf(err, "metrics client: Unknown metric name %s\\n",
                  metric_name_str.c_str());
          ShowUsage(err);
          return EXIT_FAILURE;
        }}
      }}

       if (!event.Record()) {{
         fprintf(err, "metrics client: Event recording failed.\\n");
         return EXIT_FAILURE;
       }}
       return EXIT_SUCCESS;
    }}
"""

PRE_METRIC_TEMPLATE = """\
      bool already_parsed_{metric.name} = false;
"""

METRIC_TEMPLATE = """\
        else if (metric_name == "{metric.raw_name}") {{
          if (already_parsed_{metric.name}) {{
            fprintf(err,
                    "metrics client: multiple "
                    "--{metric.raw_name} arguments.\\n"
                    "(NOTE: use --field=comma-separated-list-of-ints for "
                    "integer array types.)\\n");
            ShowUsage(err);
            return EXIT_FAILURE;
          }}
          already_parsed_{metric.name} = true;
          auto metric_value = {metric.arg_parser}(metric_value_string);
          if (!metric_value.has_value()) {{
            // metric_value_string may not be nul-terminated; turn into a
            // std::string for c_str() for %s.
            std::string metric_value_string_for_error(metric_value_string);
            fprintf(err,
                    "metrics client: Cannot parse '%s' as {metric.type_name}\\n",
                    metric_value_string_for_error.c_str());
            ShowUsage(err);
            return EXIT_FAILURE;
          }}
          event.Set{metric.name}(metric_value.value());
        }}
"""

ARRAY_METRIC_TEMPLATE = """\
        else if (metric_name == "{metric.raw_name}") {{
          if (already_parsed_{metric.name}) {{
            fprintf(err,
                    "metrics client: multiple "
                    "--{metric.raw_name} arguments.\\n"
                    "(NOTE: use --field=comma-separated-list-of-ints for "
                    "integer array types.)\\n");
            ShowUsage(err);
            return EXIT_FAILURE;
          }}
          already_parsed_{metric.name} = true;
          auto metric_value = {metric.arg_parser}(metric_value_string);
          if (!metric_value.has_value()) {{
            // metric_value_string may not be nul-terminated; turn into a
            // std::string for c_str() for %s.
            std::string metric_value_string_for_error(metric_value_string);
            fprintf(err,
                    "metrics client: Cannot parse '%s' as {metric.type_name}\\n",
                    metric_value_string_for_error.c_str());
            ShowUsage(err);
            return EXIT_FAILURE;
          }}
          if (metric_value.value().size() > event.Get{metric.name}MaxLength()) {{
            fprintf(err,
                    "metrics client: Too many values for {metric.name} "
                    "(got %d, maximum is %d)\\n",
                    static_cast<int>(metric_value.value().size()),
                    static_cast<int>(event.Get{metric.name}MaxLength()));
            return EXIT_FAILURE;
          }}
          event.Set{metric.name}(metric_value.value());
        }}
"""
