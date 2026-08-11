"""Where the TVM editor writes its output.

The pipeline steps read and write under TVMEditor.Test's build directory, so its
target framework leaks into 24 path literals across four scripts. Those literals
all said `net5.0`, which no build has produced since the .NET projects moved to
`net10.0` -- `dotnet build -c Release` writes `bin/Release/net10.0`. Keeping the
segment here means retargeting the project is one override rather than an
edit-every-string sweep, and run.sh honours the same variable.
"""

from __future__ import annotations

import os

EDITOR_SUBDIR = os.environ.get(
    "TSMC_EDITOR_BUILD", "TVMEditor.Test/bin/Release/net10.0"
)

# Relative to ./tsmc, which is where run.sh runs every Python step from.
EDITOR_BUILD = os.path.join("../tvm-editing", EDITOR_SUBDIR)
