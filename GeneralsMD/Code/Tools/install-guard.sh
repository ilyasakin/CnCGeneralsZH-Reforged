# install-guard.sh: sourced, not run. Rule 9's before-and-after check of the install, for every
# harness that roots the game at a farm of it (a farm entry is a link into the install, so a write
# through one lands in the install).
#
#   install_snapshot <install> <file>
#       Every entry under <install>: a folder, or a file's size, modification time and BLAKE2 (read
#       only). Returns non-zero, and writes nothing usable, when the listing cannot be made or written.
#   install_verify <install> <before> <after>
#       Snapshots the install again into <after> and compares. Prints one verdict and returns it:
#         0  "ok: the install is as it was (...)"
#         1  "FAIL: THE INSTALL CHANGED:" and the lines that differ
#         2  "FAIL: COULD NOT VERIFY the install: <why>", when either listing is missing or empty, or
#            the second one could not be made. Never reported as a change: the 2026-09 false alarm was
#            a run whose work folder had vanished, so the after-listing could not be written, and the
#            comparison of nothing with something printed "INSTALL CHANGED" with an empty list.
#
# A harness treats 1 and 2 alike, as a failure; only the words differ, so a reader knows which it was.

install_snapshot() {
	[ -d "$1" ] || return 1
	python3 - "$1" > "$2" <<'EOF' || return 1
import hashlib, os, sys
root = sys.argv[1]
count = 0
for base, dirs, files in os.walk(root, onerror=lambda e: sys.exit('install-guard: ' + str(e))):
    dirs.sort()
    for name in sorted(dirs + files):
        p = os.path.join(base, name)
        st = os.lstat(p)
        count += 1
        if os.path.isdir(p):
            print(os.path.relpath(p, root), 'dir'); continue
        h = hashlib.blake2b(digest_size=16)
        with open(p, 'rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                h.update(block)
        print(os.path.relpath(p, root), st.st_size, int(st.st_mtime_ns), h.hexdigest())
sys.exit(0 if count else 'install-guard: the install is empty')
EOF
	[ -s "$2" ]
}

install_verify() {
	local install="$1" before="$2" after="$3"
	if [ ! -s "$before" ]; then
		echo "FAIL: COULD NOT VERIFY the install: the listing taken before the run ($before) is missing or empty"
		return 2
	fi
	if ! install_snapshot "$install" "$after"; then
		echo "FAIL: COULD NOT VERIFY the install: its listing after the run could not be made or written ($after)"
		return 2
	fi
	if cmp -s "$before" "$after"; then
		echo "ok: the install is as it was ($(wc -l < "$before" | tr -d ' ') entries: sizes, times and contents)"
		return 0
	fi
	echo "FAIL: THE INSTALL CHANGED:"
	diff "$before" "$after" | head -10
	return 1
}
