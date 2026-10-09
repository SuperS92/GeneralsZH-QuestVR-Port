#!/usr/bin/env python3
"""Native-Python replacement for the generated Meson touch command on Windows."""
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit('Expected one generated output path')
output = pathlib.Path(sys.argv[1]).resolve()
if not output.is_relative_to(pathlib.Path.cwd().resolve()):
    raise SystemExit('Generated touch output must stay inside the build directory')
output.touch()
