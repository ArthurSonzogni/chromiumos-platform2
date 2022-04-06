# Analytics

This directory contains implementation of internal analytics of Missive via UMA.

To add a new analytics resource:

1. Create a new class `ResourceCollectorMyResource` that inherits
   `ResourceCollector`.

1. Implement `ResourceCollectorMyResource::Collect` according to the document.

1. Register it to the registry in the `MissiveDaemon` constructor by calling

   ```
   analytics_registry_.Add("MyResource", std::make_unique<ResourceCollectorMyResource>(base::Minutes(10)))
   ```

   Feel free to replace `base::Minutes(10)` above with any reasonable time
   interval.
