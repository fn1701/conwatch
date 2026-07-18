#!/usr/bin/env python3
# Strips compiler flags that GCC accepts but clang-tidy's driver rejects
# outright (e.g. -mno-direct-extern-access, injected by Qt6's CMake config
# for GCC hardening) from build/compile_commands.json, writing the result
# to build/compile_commands-clang-tidy.json for the clang-tidy pre-commit
# hook to consume via -p. The real build always uses g++ and is unaffected.
import json
import os
import sys

UNSUPPORTED_FLAGS = ["-mno-direct-extern-access"]

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(os.path.dirname(dst), exist_ok=True)
with open(src) as f:
    entries = json.load(f)

for entry in entries:
    for flag in UNSUPPORTED_FLAGS:
        entry["command"] = entry["command"].replace(flag + " ", "")

with open(dst, "w") as f:
    json.dump(entries, f, indent=2)
