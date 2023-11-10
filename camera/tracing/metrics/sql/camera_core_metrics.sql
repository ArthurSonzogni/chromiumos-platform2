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
DROP VIEW IF EXISTS slice_per_session_with_stream_conf_id;
CREATE VIEW slice_per_session_with_stream_conf_id AS
WITH
  streams_per_session AS (
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
  streams_per_session
ON (
  slice_per_session.session_id = streams_per_session.session_id AND
  slice_per_session.ts >= streams_per_session.start_ts AND
  CASE
    WHEN streams_per_session.end_ts IS NULL
      THEN TRUE
    ELSE
      slice_per_session.ts < streams_per_session.end_ts
  END
);


DROP VIEW IF EXISTS request_streams;
CREATE VIEW request_streams AS
SELECT DISTINCT
  conf_id,
  EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
  EXTRACT_ARG(arg_set_id, 'debug.width') AS width,
  EXTRACT_ARG(arg_set_id, 'debug.height') AS height,
  EXTRACT_ARG(arg_set_id, 'debug.format') AS format
FROM slice_per_session_with_stream_conf_id
WHERE name = 'Request Buffer'
ORDER BY conf_id ASC, stream_id ASC;


DROP VIEW IF EXISTS result_buffer_latencies;
CREATE VIEW result_buffer_latencies AS
WITH avg AS (
  SELECT
    conf_id,
    EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
    EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
    name, "avg" AS metric_name, "us" AS unit, AVG(dur) AS value,
    0 AS bigger_is_better
  FROM slice_per_session_with_stream_conf_id
  WHERE name = 'Result Buffer'
  GROUP BY conf_id, stream_id
)
  SELECT
    conf_id,
    EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
    EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
    name, "min" AS metric_name, "us" AS unit,
    CAST(MIN(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
  FROM slice_per_session_with_stream_conf_id
  WHERE name = 'Result Buffer'
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
  FROM
    (
      SELECT * FROM slice_per_session_with_stream_conf_id
      WHERE name = 'Result Buffer'
    ) AS s
  LEFT JOIN avg ON s.name = avg.name
  GROUP by s.conf_id, stream_id
UNION ALL
  SELECT
    conf_id,
    EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
    EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
    name, "max" AS metric_name, "us" AS unit,
    CAST(MAX(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
  FROM slice_per_session_with_stream_conf_id
  WHERE name = 'Result Buffer'
  GROUP BY conf_id, stream_id
UNION ALL
  SELECT
    conf_id,
    EXTRACT_ARG(arg_set_id, 'debug.stream') AS stream_id,
    EXTRACT_ARG(arg_set_id, 'debug.frame_number') AS frame_number,
    name, "count" AS metric_name, "count" AS unit, COUNT(dur) AS value,
    0 AS bigger_is_better
  FROM slice_per_session_with_stream_conf_id
  WHERE name = 'Result Buffer'
  GROUP BY conf_id, stream_id
ORDER BY conf_id ASC, stream_id ASC;


DROP VIEW IF EXISTS stream_metrics_per_session;
CREATE VIEW stream_metrics_per_session AS
WITH avg AS (
  SELECT
    session_id, conf_id, name, "avg" AS metric_name, "us" AS unit,
    AVG(dur) AS value, 0 AS bigger_is_better
  FROM
    slice_per_session_with_stream_conf_id
  WHERE name IN (SELECT name FROM conf_functions) AND conf_id NOT NULL
  GROUP BY
    session_id, conf_id, name
)
  SELECT
    session_id, conf_id, name, "min" AS metric_name, "us" AS unit,
    CAST(MIN(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
  FROM
    slice_per_session_with_stream_conf_id
  WHERE name IN (SELECT name FROM conf_functions) AND conf_id NOT NULL
  GROUP BY
    session_id, conf_id, name
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
  FROM
    (
      SELECT * FROM slice_per_session_with_stream_conf_id
      WHERE name IN (SELECT name FROM conf_functions)
    ) AS s
  LEFT JOIN avg ON s.name = avg.name
  GROUP BY s.session_id, s.conf_id, s.name
UNION ALL
  SELECT
    session_id, conf_id, name, "max" AS metric_name, "us" AS unit,
    CAST(MAX(dur) / 1e3 as INT) AS value, 0 AS bigger_is_better
  FROM
    slice_per_session_with_stream_conf_id
  WHERE name IN (SELECT name FROM conf_functions) AND conf_id NOT NULL
  GROUP BY
    session_id, conf_id, name
UNION ALL
  SELECT
    session_id, conf_id, name, "count" AS metric_name, "count" AS unit,
    COUNT(dur) AS value, 0 AS bigger_is_better
  FROM
    slice_per_session_with_stream_conf_id
  WHERE name IN (SELECT name FROM conf_functions) AND conf_id NOT NULL
  GROUP BY
    session_id, conf_id, name;


DROP VIEW IF EXISTS session_metrics;
CREATE VIEW session_metrics AS
WITH avg AS (
  SELECT
    session_id, name, "avg" AS metric_name, "us" AS unit,
    AVG(dur) AS value, 0 AS bigger_is_better
  FROM slice_per_session
  WHERE name IN (SELECT name FROM session_functions)
  GROUP BY session_id, name
)
  SELECT
    session_id, name, "min" AS metric_name, "us" AS unit,
    CAST(MIN(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
  FROM slice_per_session
  WHERE name IN (SELECT name FROM session_functions)
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
  FROM
    (
      SELECT * FROM slice_per_session
      WHERE name IN (SELECT name FROM session_functions)
    ) AS s
  LEFT JOIN avg ON s.name = avg.name
  GROUP BY s.session_id, s.name
UNION ALL
  SELECT
    session_id, name, "max" AS metric_name, "us" AS unit,
    CAST(MAX(dur) / 1e3 AS INT) AS value, 0 AS bigger_is_better
  FROM slice_per_session
  WHERE name IN (SELECT name FROM session_functions)
  GROUP BY session_id, name
UNION ALL
  SELECT
    session_id, name, "count" AS metric_name, "count" AS unit,
    COUNT(dur) AS value, 0 AS bigger_is_better
  FROM slice_per_session
  WHERE name IN (SELECT name FROM session_functions)
  GROUP BY session_id, name;


DROP VIEW IF EXISTS camera_core_metrics_output;
CREATE VIEW camera_core_metrics_output AS
SELECT CameraCoreMetricsPerSession(
  'sessions', (
    SELECT RepeatedField(
      CameraCoreMetrics(
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
        ),

        'stream_metrics', (
          SELECT RepeatedField(
            CameraStreamMetrics(
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
                FROM stream_metrics_per_session AS stream
                WHERE distinct_stream.conf_id = stream.conf_id
              ),

              'result_buffer_metrics', (
                SELECT RepeatedField(
                  CaptureResultBufferMetrics(
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
                      FROM result_buffer_latencies AS result
                      WHERE distinct_result.stream_id = result.stream_id
                    )
                  )
                )
                FROM (
                  SELECT DISTINCT session_id, conf_id, stream_id
                  FROM result_buffer_latencies
                ) AS distinct_result
                JOIN request_streams USING (conf_id, stream_id)
                WHERE distinct_result.conf_id = distinct_stream.conf_id
              )
            )
          )
          FROM (
            SELECT DISTINCT session_id, conf_id FROM stream_metrics_per_session
          ) AS distinct_stream
          WHERE
            distinct_sess.session_id = distinct_stream.session_id
          ORDER BY
            distinct_stream.conf_id ASC
        )
      )
    )
  )
)
FROM (SELECT DISTINCT session_id FROM session_metrics) AS distinct_sess;
