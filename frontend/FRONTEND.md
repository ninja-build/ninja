Ninja Frontend Interface
========================

Ninja can use another program as a frontend to display build status information.
This document describes the interface between Ninja and the frontend.

Connecting
----------

The frontend is passed to Ninja using a --frontend argument.  The argument is
executed the same as a build rule Ninja, wrapped in `sh -c` on Linux.  The
frontend will be executed with the read end of a pipe open on file descriptor
`3`.

Ninja will pass [Protocol Buffers](https://developers.google.com/protocol-buffers/) generated from src/frontend.proto.

stdin/stdout/stderr
-------------------

The frontend will have stdin, stdout, and stderr connected to the same file
descriptors as Ninja.  The frontend MAY read from stdin, however, if it does,
it MUST NOT read from stdin whenever a job in the console pool is running,
from when an `EdgeStarted` message is received with the `use_console` value
set to `true`, to when an `EdgeFinished` message is received with the same value
for `id`.  Console rules may write directly to the same stdout/stderr as the
frontend.

Exiting
-------

The frontend MUST exit when the input pipe on fd `3` is closed.  When a build
finishes, either successfully, due to error, or on interrupt, Ninja will close
the pipe and then block until the frontend exits.

Experimenting with frontends
----------------------------

To run Ninja with a frontend that mimics the behavior of Ninja's normal output:
```
$ ./ninja --frontend=frontend/native.py
```

To save serialized output to a file:
```
$ ./ninja --frontend='cat <&3 > ninja.pb' all
```

To run a frontend with serialized input from a file:
```
$ frontend/native.py 3< ninja.pb
```
