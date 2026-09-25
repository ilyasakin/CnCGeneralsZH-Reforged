#!/usr/bin/env python3
"""What a change looks like to MSVC: the Windows view of every C/C++ file it touches, diffed.

There is no Windows machine on this project, and mingw is not a stand-in: it defines _WIN32 but not
_MSC_VER, so it takes the POSIX side of every `#if defined(_MSC_VER)`, which is most of this tree's
platform code. This resolves MSVC's conditionals textually instead. It runs unifdef with the macros
MSVC x64 defines set and the ones it never defines unset, on the before and after of each changed
file. Then it strips comments and blank lines and diffs. What remains is what the Windows compiler
sees differently.

Every changed file lands in one of three buckets:
  identical      the change lives in comments or in branches MSVC never takes
  include case   only the case of #include lines differs; NTFS folds case, so the same file
  different      anything else, printed line by line. Each one wants a WINDOWS-DEBT.md row.

  windows_view_diff.py                  merge-base with feature/mac-port .. HEAD
  windows_view_diff.py BASE [HEAD]      any two revisions; HEAD may be WORKTREE for uncommitted work

WHAT IT CANNOT SEE:
  - Macro-expanded tokens. A new `WW_NOEXCEPT_DELETE` shows up as that token; what it expands to
    under MSVC is only visible where the macro is defined, if that file is in the change. Check it.
  - Headers it does not follow. Each file is resolved on its own. A change to a header shows in
    that header, not in the files that include it, and a #define in one file does not resolve an
    #if in another (cpudetect.h's CPUDETECT_X86 comes from _M_X64 through a #define, so an
    `#ifdef CPUDETECT_X86` elsewhere stays unresolved and both branches are compared).
  - Conditionals on anything but the macros below: project macros, SDK macros, _DEBUG. Those are
    left in place and compared as text on both sides.
  - Anything that is not C or C++: CMake, scripts, data.
  - Whether MSVC accepts the result. This is a text diff, not a compiler.

It fails, rather than reporting clean, on an empty change, a missing unifdef, or a control run
that shows the defines had no effect.
"""
import difflib
import re
import subprocess
import sys

# MSVC x64: what it defines, and what it never does.
MSVC_DEFINES = ['-D_MSC_VER=1930', '-D_WIN32=1', '-DWIN32=1', '-D_WIN64=1', '-D_M_X64=100',
                '-D_M_AMD64=100']
NOT_MSVC = ['-U__clang__', '-U__GNUC__', '-U__APPLE__', '-U__MACH__', '-U__linux__',
            '-U__unix__', '-U__GLIBC__', '-U__x86_64__', '-U__aarch64__', '-U__arm64__',
            '-UZH_PLATFORM_POSIX']
# The control: the same files with the Windows macros unset must look different somewhere,
# or the defines above resolved nothing and every "identical" below would be meaningless.
POSIX_VIEW = ['-U_MSC_VER', '-U_WIN32', '-UWIN32', '-U_WIN64', '-U_M_X64', '-U_M_AMD64']

SOURCE = re.compile(r'\.(h|hpp|hh|inl|c|cc|cpp|cxx)$', re.I)
INCLUDE = re.compile(r'\s*#\s*include\b')
# A diagnostic's text is prose, and unifdef reads an apostrophe in it as an unterminated character
# literal ("#error ... the engine's spelling").  Its quotes become backquotes on both sides, so the
# message is still compared.
DIAGNOSTIC = re.compile(r'^(\s*#\s*(?:error|warning)\b)(.*)$', re.M)


def git(*args):
    return subprocess.run(['git', *args], capture_output=True, check=True).stdout


def read(rev, path):
    if rev == 'WORKTREE':
        with open(path, 'rb') as f:
            return f.read()
    return git('show', f'{rev}:{path}')


def strip_comments(text):
    """Remove // and /* */ comments, respecting string and character literals; keep line count.

    Done BEFORE unifdef, not after: unifdef parses comments itself and gives up on some of this
    tree's ("EOF in comment ... output may be truncated"), and a truncated view compares as equal
    to another truncated view.  An earlier version of this check trusted that output.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                i += 1
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            end = text.find('*/', i + 2)
            end = n if end < 0 else end + 2
            out.append('\n' * text.count('\n', i, end))
            i = end
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def view(data, flags):
    text = strip_comments(data.decode('latin-1').replace('\r\n', '\n'))
    text = DIAGNOSTIC.sub(lambda m: m.group(1) + m.group(2).replace("'", '`').replace('"', '`'), text)
    run = subprocess.run(['unifdef', *flags], input=text.encode('latin-1'), capture_output=True)
    if run.returncode not in (0, 1):            # 0 unchanged, 1 changed, 2 trouble
        raise RuntimeError(run.stderr.decode('utf-8', 'replace').strip() or 'unifdef failed')
    text = run.stdout.decode('latin-1')
    return [line.rstrip() for line in text.split('\n') if line.strip()]


def main():
    args = sys.argv[1:]
    if args and args[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    try:
        subprocess.run(['unifdef', '-h'], capture_output=True)
    except OSError:
        print('windows_view_diff: FAIL - unifdef is not installed; nothing was compared')
        return 2
    head = args[1] if len(args) > 1 else 'HEAD'
    base = args[0] if args else git('merge-base', 'feature/mac-port', 'HEAD').decode().strip()

    status = ['diff', '-M', '--name-status', base] + ([] if head == 'WORKTREE' else [head])
    changed = []
    for line in git(*status, '--', '.').decode().splitlines():
        parts = line.split('\t')
        kind = parts[0]
        if kind.startswith('R'):
            old, new = parts[1], parts[2]
        elif kind == 'M':
            old = new = parts[1]
        elif kind in ('A', 'D'):
            old = new = parts[1]
        else:
            continue
        if SOURCE.search(new):
            changed.append((kind, old, new))
    if not changed:
        print(f'windows_view_diff: FAIL - no C/C++ file changed between {base[:10]} and {head}; '
              'there is nothing to compare, which is not the same as nothing differing')
        return 2

    top = git('rev-parse', '--show-toplevel').decode().strip()
    buckets = {'identical': [], 'include case': [], 'different': [], 'added': [], 'deleted': []}
    details = {}
    control_differs = 0
    for kind, old, new in changed:
        try:
            if kind == 'A':
                buckets['added'].append(new)
                continue
            if kind == 'D':
                buckets['deleted'].append(old)
                continue
            before = view(read(base, old), MSVC_DEFINES + NOT_MSVC)
            after_raw = read(head, f'{top}/{new}' if head == 'WORKTREE' else new)
            after = view(after_raw, MSVC_DEFINES + NOT_MSVC)
            if view(after_raw, POSIX_VIEW + NOT_MSVC) != after:
                control_differs += 1
        except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
            print(f'windows_view_diff: FAIL - could not resolve {new}: {error}')
            return 2
        minus = [l[2:] for l in difflib.ndiff(before, after) if l.startswith('- ')]
        plus = [l[2:] for l in difflib.ndiff(before, after) if l.startswith('+ ')]
        if not minus and not plus:
            buckets['identical'].append(new)
        elif (len(minus) == len(plus) and all(INCLUDE.match(m) for m in minus)
              and all(m.lower() == p.lower() for m, p in zip(minus, plus))):
            buckets['include case'].append(new)
        else:
            buckets['different'].append(new)
            details[new] = (minus, plus)

    if control_differs == 0:
        print('windows_view_diff: FAIL - control: no changed file looks any different with the '
              'Windows macros unset, so the defines resolved nothing and this run proves nothing')
        return 2

    print(f'windows_view_diff: {len(changed)} C/C++ files, {base[:10]} .. {head}, as MSVC x64 sees them')
    print(f'  control: {control_differs} of them read differently with the Windows macros unset')
    for name in ('identical', 'include case', 'different', 'added', 'deleted'):
        print(f'  {len(buckets[name]):5d}  {name}')
    for name in ('added', 'deleted'):
        for path in buckets[name]:
            print(f'  {name}: {path} - new text, read it whole')
    for path in buckets['different']:
        minus, plus = details[path]
        print(f'\n  {path}')
        for line in minus:
            print(f'    - {line[:160]}')
        for line in plus:
            print(f'    + {line[:160]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
