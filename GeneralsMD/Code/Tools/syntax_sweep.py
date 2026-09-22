#!/usr/bin/env python3
"""Compile each of a CMake target's sources on its own and report the first real error.

    python3 Tools/syntax_sweep.py build-mac wwlib
    python3 Tools/syntax_sweep.py build-mac gameengine --files      # list names per cause
    python3 Tools/syntax_sweep.py build-mac wwmath --only lookuptable.cpp

Why this exists.  A library that does not link tells you almost nothing about how far it is from
linking: `make` stops, or stops eighty times on the same header, and a grep over the output answers
a different question than the one you asked.  Three people measured wwmath three different ways in
one afternoon and got three different tables.  Compiling each source separately with -fsyntax-only
and -ferror-limit=1 answers exactly "how many of these would compile, and what is the first thing
stopping each of the rest", which is the number worth tracking and the only one that has held up.

Two details that matter, both of which the obvious implementation gets wrong:

  * Attribute the failure to the line that says `error:`, NOT to a filename grepped out of the
    output.  clang prints an `In file included from ...` chain before the diagnostic, so grepping
    for filenames attributes every failure to whichever header happens to appear first in the
    chain.  That is how the contradictory wwmath tables happened.  The regex below anchors on
    `<path>:<line>:<col>: error:` for that reason - do not "simplify" it to a filename match.

  * -ferror-limit=1 on purpose.  We want the FIRST cause per file, because fixing it usually
    reveals a different second cause and counting all of them makes the same header look like
    twenty problems.  Re-run after each fix; the tail changes shape as you go.

It reads the source list and the flags out of the build tree rather than taking them on the command
line, so it cannot disagree with what CMake actually compiles.
"""

import argparse, collections, os, re, subprocess, sys

ERROR_LINE = re.compile(r"^(/[^:]+):(\d+):\d+: (?:fatal )?error: (.*)$")


def target_sources(build, target):
    """The sources CMake compiles for this target, from its DependInfo."""
    p = os.path.join(build, "CMakeFiles", target + ".dir", "DependInfo.cmake")
    if not os.path.exists(p):
        sys.exit("no such target in %s: %s (looked for %s)" % (build, target, p))
    # DependInfo maps "<absolute source>" "<object>"; take the first of each pair.
    return sorted(set(re.findall(r'"(/[^"]+\.(?:cpp|CPP|c|cc|cxx))"\s+"', open(p).read())))


def target_flags(build, target):
    """The include set and compile flags CMake uses, from flags.make."""
    p = os.path.join(build, "CMakeFiles", target + ".dir", "flags.make")
    if not os.path.exists(p):
        sys.exit("no flags.make for %s; configure the build tree first" % target)
    inc, flags = "", ""
    for line in open(p):
        # CMake may suffix these per-architecture, e.g. CXX_FLAGSarm64.
        if line.startswith("CXX_INCLUDES"):
            inc = line.split("=", 1)[1].strip()
        elif line.startswith("CXX_FLAGS"):
            flags = line.split("=", 1)[1].strip()
    return inc.split(), flags.split()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("target")
    ap.add_argument("--only", help="sweep just the sources whose name contains this")
    ap.add_argument("--files", action="store_true", help="list the failing file names per cause")
    ap.add_argument("--cxx", default="clang++")
    args = ap.parse_args()

    srcs = target_sources(args.build, args.target)
    if args.only:
        srcs = [s for s in srcs if args.only in os.path.basename(s)]
    inc, flags = target_flags(args.build, args.target)

    ok, causes = [], collections.defaultdict(list)
    for src in srcs:
        cmd = [args.cxx, "-fsyntax-only", "-ferror-limit=1"] + flags + inc + [src]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode == 0:
            ok.append(os.path.basename(src))
            continue
        for line in r.stderr.split("\n"):
            m = ERROR_LINE.match(line)
            if m:
                where = "%s:%s" % (os.path.basename(m.group(1)), m.group(2))
                causes[(where, m.group(3))].append(os.path.basename(src))
                break
        else:
            first = (r.stderr.strip().split("\n") or ["(no diagnostic)"])[0]
            causes[("?", first[:90])].append(os.path.basename(src))

    print("%s: %d of %d compile, %d fail" % (args.target, len(ok), len(srcs), len(srcs) - len(ok)))
    if causes:
        print()
    for (where, msg), files in sorted(causes.items(), key=lambda kv: -len(kv[1])):
        print("%3d  %-28s %s" % (len(files), where, msg[:70]))
        if args.files:
            print("     " + " ".join(sorted(files)))
    return 1 if causes else 0


if __name__ == "__main__":
    sys.exit(main())
