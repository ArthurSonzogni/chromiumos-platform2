# ARC File Syncer

`arc-file-syncer` performs bi-directional file synchronization for the set of
 predefined control files. For each file it creates copy of it and activates
 watching for file change. Each change in the source file is propagated to
 the copy file. And change in the copy file is propagated to the source file.
 This allows creating bind mapping of the copy file in container namespace.
 As a result the content of source file in host namespace and container
 namespace is synchronized.
