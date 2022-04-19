# Parallax: Visual Analysis Framework

## Summary:

Parallax provides a set of utilities to help engineers visualize and process
datasets created by our assortment of tools and logs. The goal of this
framework is simplify processes involved in diagnosing defects by providing
an interface that allows users to:

* View and interact with complex datasets
* Compare different devices or tests
* Save or parse data from an assortment of formats
* Perform automatic checks against a report
* Stream and view measurements in real-time

## Components and Initial Roadmap:

Components of this project can act as self-contained tools or as modules
which can be imported by other systems:

`The 2022-Q2 road-map is to incorporate the following:`

### Self-contained tools:

* Test Report Processor:
  * Evaluates test reports and compares against a set of targets.
  * Allows multiple test reports to be compared side by side to identify
  variations.
  * Support for power and thermal tests.
  * Supports user definable configuration files to replace targets.
* Real-time Measurement Viewer:
  * Enables real-time viewing of time series measurements by power monitoring
  tools.

### Helper Modules:

* Measurement I/O:
  * Creates interface to store and load measurements as a standardized format
  with the goal of reducing the proliferation of file formats.
  * Enables queries on the data to evaluate statistical features.
  * Export tools to allow other consumers to process the data.

* Measurement Streaming:
  * Designed to stream measurements to the Viewer enabling users to view
  information from tools like Monkey Island and Sweetberry in real-time.
