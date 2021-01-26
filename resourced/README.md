# resourced - Chrome OS Resource Management Daemon

resourced supports the following 2 D-Bus interfaces for resource management.
Check go/resourced for details.

*   org.chromium.MemoryPressure - low memory notification API
    *   When memory pressure is high, notifying subsystems to free memory.
    *   Method GetAvailableMemoryKB - returns the available memory.
    *   Method GetMemoryMarginsKB - returns the margin (threshold) for critical
        and moderate memory pressure.
