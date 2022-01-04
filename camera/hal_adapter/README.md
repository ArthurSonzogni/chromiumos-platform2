# How to collect and view camera traces

We use Perfetto to record camera traces on Chrome and CrOS Camera Service.
Therefore, we can collect camera traces from these components to evaluate the
performance of each steps when setting up the camera stream and understand the
journey of a camera capture request.

To do so, currently we can run perfetto command line tool while using the
camera.

**Step 1**:

Run the perfetto command line to collect camera traces from components.

You can specify how long you want to record by `duration_ms` field. Example:

```
perfetto -c - --txt -o /tmp/perfetto-trace \
<<EOF

buffers: {
    size_kb: 63488
    fill_policy: DISCARD
}
buffers: {
    size_kb: 63488
    fill_policy: DISCARD
}
data_sources: {
    config {
        name: "track_event"
        target_buffer: 0
        track_event_config {
            enabled_categories: "cros_camera"
        }
    }
}
data_sources: {
    config {
        name: "org.chromium.trace_event"
        target_buffer: 1
        chrome_config {
            trace_config: "{\"record_mode\":\"record-until-full\",\"included_categories\":[\"camera\"],\"memory_dump_config\":{}}"
        }
    }
}
duration_ms: 20000

EOF
```

**Step 2**:

While perfetto command is recording, open up a camera application and manipulate
the flow you are interested in. (e.g. Taking a picture or recording a video)

**Step 3**:

Go to Perfetto UI (https://ui.perfetto.dev/), click "Open trace file" and
provide the result file (in our example is `/tmp/perfetto-trace`).

The details of the tracing results should be shown on the UI.

<!---
TODO(b/212231270): Add instructions about how to use Perfett UI directly to
collect camera traces sent from each platforms once Perfetto UI supports
custom configuration.
-->
