-- Copyright 2022 The ChromiumOS Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.

SELECT RUN_METRIC('camera_sessions.sql');


DROP TABLE IF EXISTS session_functions;
CREATE TABLE session_functions (name varchar(80));
INSERT INTO session_functions VALUES
  -- OpenDevice
  ("CameraModuleDelegate::OpenDevice"),
  ("CameraHalAdapter::OpenDevice"),
  ("HAL::OpenDevice"),

  -- Initialize
  ("Camera3DeviceOpsDelegate::Initialize"),
  ("CameraDeviceAdapter::Initialize"),
  ("StreamManipulatorManager::Initialize"),
  ("HAL::Initialize"),

  -- Close
  ("Camera3DeviceOpsDelegate::Close"),
  ("CameraDeviceAdapter::Close"),
  ("StreamManipulatorManager::~StreamManipulatorManager"),
  ("CameraHalAdapter::CloseDevice"),
  ("CameraDeviceAdapter::~CameraDeviceAdapter");


DROP TABLE IF EXISTS conf_functions;
CREATE TABLE conf_functions (name varchar(80));
INSERT INTO conf_functions VALUES
  -- ConfigureStream
  ("Camera3DeviceOpsDelegate::ConfigureStreams"),
  ("CameraDeviceAdapter::ConfigureStreams"),
  ("StreamManipulatorManager::ConfigureStreams"),
  ("HAL::ConfigureStreams"),

  --ConstructDefaultRequestSetting
  ("Camera3DeviceOpsDelegate::ConstructDefaultRequestSettings"),
  ("CameraDeviceAdapter::ConstructDefaultRequestSettings"),
  ("StreamManipulatorManager::ConstructDefaultRequestSettings"),

  --ProcessCaptureRequest
  ("Camera3DeviceOpsDelegate::ProcessCaptureRequest"),
  ("CameraDeviceAdapter::ProcessCaptureRequest"),
  ("StreamManipulatorManager::ProcessCaptureRequest"),
  ("HAL::ProcessCaptureRequest");


-- The camera client can (re-)configure different stream sets in one camera
-- session.
DROP VIEW IF EXISTS slice_per_session_with_conf_id;
CREATE VIEW slice_per_session_with_conf_id AS
WITH
  config_per_session AS (
    SELECT
      *,
      ROW_NUMBER() OVER () AS conf_id,
      ts AS start_ts,
      LEAD(ts, 1) OVER (PARTITION BY session_id) AS end_ts
    FROM
      slice_per_session
    WHERE
      name = 'Camera3DeviceOpsDelegate::ConfigureStreams'
    ORDER BY
      session_id ASC, start_ts ASC
  )
SELECT
  *
FROM
  slice_per_session
LEFT JOIN
  config_per_session
ON (
  slice_per_session.session_id = config_per_session.session_id AND
  slice_per_session.ts >= config_per_session.start_ts AND
  CASE
    WHEN config_per_session.end_ts IS NULL
      THEN TRUE
    ELSE
      slice_per_session.ts < config_per_session.end_ts
  END
);


-- The configuration ID, stream ID, width, height, format of a request stream.
DROP VIEW IF EXISTS request_streams;
CREATE VIEW request_streams AS
SELECT DISTINCT
  conf_id,
  EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
  EXTRACT_ARG(arg_set_id, 'debug.width') AS width,
  EXTRACT_ARG(arg_set_id, 'debug.height') AS height,
  EXTRACT_ARG(arg_set_id, 'debug.format') AS format
FROM slice_per_session_with_conf_id
WHERE name = 'Request Buffer'
ORDER BY conf_id ASC, stream_id ASC;


-- Create a view that calculate the minimum, average, standard deviation,
-- maximum and count of the duration for each configuration ID, stream ID for
-- the lifetime of a result buffer.
DROP VIEW IF EXISTS result_buffer_metrics;
CREATE VIEW result_buffer_metrics AS
WITH
  avg AS (
    SELECT
      conf_id,
      EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
      EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
      name, "avg" AS metric_name, "us" AS unit, AVG(dur) AS value,
      0 AS bigger_is_better
    FROM slice_per_session_with_conf_id
    WHERE name = 'Result Buffer'
    GROUP BY conf_id, stream_id
  ), result_slices AS (
    SELECT *
    FROM slice_per_session_with_conf_id
    WHERE name = 'Result Buffer'
  )
SELECT
  conf_id,
  EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
  EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
  name, "min" AS metric_name, "us" AS unit,
  CAST(MIN(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
FROM result_slices
GROUP BY conf_id, stream_id
UNION ALL
SELECT
  conf_id, stream_id, frame_number, name, metric_name, unit,
  CAST(value / 1e3 AS INT) AS value, bigger_is_better
FROM avg
UNION ALL
SELECT
  s.conf_id AS conf_id,
  EXTRACT_ARG(s.arg_set_id, 'debug.stream') AS stream_id,
  EXTRACT_ARG(s.arg_set_id, 'debug.frame_number') AS frame_number,
  s.name AS name, "stddev" AS metric_name, "us" AS unit,
  CAST(SQRT(AVG(POWER(s.dur - avg.value, 2))) / 1e3 AS INT) AS value,
  0 AS bigger_is_better
FROM result_slices AS s
LEFT JOIN avg ON s.name = avg.name
GROUP by s.conf_id, stream_id
UNION ALL
SELECT
  conf_id,
  EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
  EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
  name, "max" AS metric_name, "us" AS unit,
  CAST(MAX(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
FROM result_slices
GROUP BY conf_id, stream_id
UNION ALL
SELECT
  conf_id,
  EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
  EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
  name, "count" AS metric_name, "count" AS unit, COUNT(dur) AS value,
  0 AS bigger_is_better
FROM result_slices
GROUP BY conf_id, stream_id;


-- Collect latency between key time points in a frame lifetime: exposure,
-- v4l2_frame_sync_event, dqbuf, first capture result functions at different
-- levels with at least one output buffer.
DROP VIEW IF EXISTS frame_latency_slices;
CREATE VIEW frame_latency_slices AS
WITH frame_latency_time_points AS (
  SELECT * FROM (
    SELECT
      session_id, conf_id,
      EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
      min(ts) AS device_adapter_start
    FROM slice_per_session_with_conf_id
    WHERE
      name =  'CameraDeviceAdapter::ProcessCaptureResult' AND
      EXTRACT_ARG(arg_set_id, 'debug.num_output_buffers') > 0
    GROUP BY session_id, conf_id, frame_number
  ) LEFT JOIN (
    SELECT
      session_id, conf_id,
      EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
      min(ts) AS delegate_start
    FROM slice_per_session_with_conf_id
    WHERE
      name =  'Camera3CallbackOpsDelegate::ProcessCaptureResultOnThread' AND
      EXTRACT_ARG(arg_set_id, 'debug.num_output_buffers') > 0
    GROUP BY session_id, conf_id, frame_number
  ) USING (conf_id, frame_number) LEFT JOIN (
    SELECT
      session_id, conf_id,
      EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
      EXTRACT_ARG(arg_set_id, 'debug.frame_sequence') AS frame_sequence,
      min(ts) AS dqbuf_start,
      min(EXTRACT_ARG(arg_set_id, 'debug.exposure_to_dqbuf_latency_ns')) AS exposure_to_dqbuf_latency
    FROM slice_per_session_with_conf_id
    WHERE name =  'VIDOC_DQBUF'
    GROUP BY session_id, conf_id, frame_number, frame_sequence
  ) USING (conf_id, frame_number) LEFT JOIN (
    SELECT
      session_id, conf_id,
      EXTRACT_ARG(arg_set_id, 'debug.frame_sequence') AS frame_sequence,
      min(ts) AS frame_sync_start
    FROM slice_per_session_with_conf_id
    WHERE name =  'V4L2_EVENT_FRAME_SYNC'
    GROUP BY session_id, conf_id, frame_sequence
  ) USING (conf_id, frame_sequence)
) SELECT *
FROM (
  SELECT
    session_id, conf_id, frame_number,
    exposure_to_dqbuf_latency - dqbuf_start + frame_sync_start AS dur,
    "exposure_to_frame_sync_start" AS name
  FROM frame_latency_time_points
  UNION ALL
  SELECT
    session_id, conf_id, frame_number,
    dqbuf_start - frame_sync_start AS dur,
    "frame_sync_start_to_dqbuf_start" AS name
  FROM frame_latency_time_points
  UNION ALL
  SELECT
    session_id, conf_id, frame_number,
    device_adapter_start - dqbuf_start AS dur,
    "dqbuf_start_to_device_adapter_start" AS name
  FROM frame_latency_time_points
  UNION ALL
  SELECT
    session_id, conf_id, frame_number,
    delegate_start - device_adapter_start AS dur,
    "device_adapter_start_to_delegate_start" AS name
  FROM frame_latency_time_points
);


-- Create a view that calculate the minimum, average, standard deviation,
-- maximum and count of the duration for each session ID, configuration ID and
-- function name for functions in |conf_functions|.
DROP VIEW IF EXISTS conf_metrics;
CREATE VIEW conf_metrics AS
WITH
  avg AS (
    SELECT
      session_id, conf_id, name, "avg" AS metric_name, "us" AS unit,
      AVG(dur) AS value, 0 AS bigger_is_better
    FROM conf_slices
    GROUP BY session_id, conf_id, name
  ),
  conf_slices AS (
    SELECT session_id, conf_id, dur, name
    FROM slice_per_session_with_conf_id
    WHERE name IN (SELECT name FROM conf_functions) AND conf_id NOT NULL
    UNION ALL
    SELECT session_id, conf_id, dur, name
    FROM frame_latency_slices
  )
SELECT
  session_id, conf_id, name, "min" AS metric_name, "us" AS unit,
  CAST(MIN(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
FROM conf_slices
GROUP BY session_id, conf_id, name
UNION ALL
SELECT
  session_id, conf_id, name, metric_name, unit,
  CAST(value / 1e3 AS INT) AS value, bigger_is_better
FROM avg
UNION ALL
SELECT
  s.session_id AS session_id, s.conf_id as conf_id, s.name AS name,
  "stddev" AS metric_name, "us" AS unit,
  CAST(SQRT(AVG(POWER(s.dur - avg.value, 2))) / 1e3 AS INT) AS value,
  0 AS bigger_is_better
FROM conf_slices AS s
LEFT JOIN avg ON s.name = avg.name
GROUP BY s.session_id, s.conf_id, s.name
UNION ALL
SELECT
  session_id, conf_id, name, "max" AS metric_name, "us" AS unit,
  CAST(MAX(dur) / 1e3 as INT) AS value, 0 AS bigger_is_better
FROM conf_slices
GROUP BY session_id, conf_id, name
UNION ALL
SELECT
  session_id, conf_id, name, "count" AS metric_name, "count" AS unit,
  COUNT(dur) AS value, 0 AS bigger_is_better
FROM conf_slices
GROUP BY session_id, conf_id, name;


-- Create a view that calculate the minimum, average, standard deviation,
-- maximum and count of the duration for each session ID and function name for
-- functions in |session_functions|.
DROP VIEW IF EXISTS session_metrics;
CREATE VIEW session_metrics AS
WITH
  avg AS (
    SELECT
      session_id, name, "avg" AS metric_name, "us" AS unit,
      AVG(dur) AS value, 0 AS bigger_is_better
    FROM slice_per_session
    WHERE name IN (SELECT name FROM session_functions)
    GROUP BY session_id, name
  ), session_slices AS (
    SELECT *
    FROM slice_per_session
    WHERE name IN (SELECT name FROM session_functions)
  )
SELECT
  session_id, name, "min" AS metric_name, "us" AS unit,
  CAST(MIN(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
FROM session_slices
GROUP BY session_id, name
UNION ALL
SELECT
  session_id, name, metric_name, unit, CAST(value / 1e3 AS INT) AS value,
  bigger_is_better
FROM avg
UNION ALL
SELECT
  s.session_id AS session_id, s.name AS name, "stddev" AS metric_name,
  "us" AS unit,
  CAST(SQRT(AVG(POWER(s.dur - avg.value, 2))) / 1e3 AS INT) AS value,
  0 AS bigger_is_better
FROM session_slices AS s
LEFT JOIN avg ON s.name = avg.name
GROUP BY s.session_id, s.name
UNION ALL
SELECT
  session_id, name, "max" AS metric_name, "us" AS unit,
  CAST(MAX(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
FROM session_slices
GROUP BY session_id, name
UNION ALL
SELECT
  session_id, name, "count" AS metric_name, "count" AS unit,
  COUNT(dur) AS value, 0 AS bigger_is_better
FROM session_slices
GROUP BY session_id, name;


DROP VIEW IF EXISTS camera_core_metrics_output;
CREATE VIEW camera_core_metrics_output AS
SELECT CameraCoreMetricsPerSession(
  'session_metrics', (
    SELECT RepeatedField(
      SessionMetrics(
        'sid', distinct_sess.session_id,
        'function_metrics', (
          SELECT RepeatedField(
            FunctionMetrics(
              'function_name', name,
              'metric_name', metric_name,
              'unit', unit,
              'value', value,
              'bigger_is_better', bigger_is_better
            )
          )
          FROM session_metrics AS sess
          WHERE distinct_sess.session_id = sess.session_id
          ORDER BY name, metric_name ASC
        ),

        'config_metrics', (
          SELECT RepeatedField(
            ConfigMetrics(
              'function_metrics', (
                SELECT RepeatedField(
                  FunctionMetrics(
                    'function_name', name,
                    'metric_name', metric_name,
                    'unit', unit,
                    'value', value,
                    'bigger_is_better', bigger_is_better
                  )
                )
                FROM conf_metrics AS conf
                WHERE distinct_conf.conf_id = conf.conf_id
                ORDER BY name, metric_name ASC
              ),

              'result_buffer_metrics', (
                SELECT RepeatedField(
                  ResultBufferMetrics(
                    'stream', Stream(
                      'stream_id', request_streams.stream_id,
                      'width', request_streams.width,
                      'height', request_streams.height,
                      'format', request_streams.format
                    ),
                    'function_metrics', (
                      SELECT RepeatedField(
                        FunctionMetrics(
                          'function_name', name,
                          'metric_name', metric_name,
                          'unit', unit,
                          'value', value,
                          'bigger_is_better', bigger_is_better
                        )
                      )
                      FROM result_buffer_metrics AS result_buffer
                      WHERE result_buffer.stream_id = distinct_result_buffer.stream_id
                      ORDER BY name, metric_name ASC
                    )
                  )
                )
                FROM (
                  SELECT DISTINCT conf_id, stream_id
                  FROM result_buffer_metrics
                ) AS distinct_result_buffer
                LEFT JOIN request_streams USING (conf_id, stream_id)
                WHERE distinct_result_buffer.conf_id = distinct_conf.conf_id
                ORDER BY distinct_result_buffer.stream_id ASC
              )
            )
          )
          FROM (
            SELECT DISTINCT session_id, conf_id FROM conf_metrics
          ) AS distinct_conf
          WHERE
            distinct_sess.session_id = distinct_conf.session_id
          ORDER BY
            distinct_conf.conf_id ASC
        )
      )
    )
  )
)
FROM (SELECT DISTINCT session_id FROM session_metrics) AS distinct_sess
ORDER BY distinct_sess.session_id ASC;
