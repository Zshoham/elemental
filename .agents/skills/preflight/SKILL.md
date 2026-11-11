---
name: preflight
description: Preflight an Elemental change - run every preset, the format and link checks, and reconcile the numbers and claims the docs record. Use before reporting any change to this repository as done, and before a commit.
---

# Preflight

A change is done when every step below ends on its criterion, or the report
names the step that did not and why.

## Steps

1. **Provision**, only if `.deps/` is missing: `cmake -P tools/provision.cmake`.
   It is the one step that uses the network.

2. **Run every preset** in `CMakePresets.json`:

   ```sh
   for p in $(cmake --list-presets | sed -n 's/^ *"\([^"]*\)".*/\1/p'); do
     cmake --preset $p && cmake --build --preset $p && ctest --preset $p || break
   done
   ```

   Done when each exits 0 and no configure or build output contains
   `warning`. While iterating, `ctest --preset <p> -LE integration` leaves
   out the slow cases.

   Start a preset from a fresh directory (`rm -rf build/<preset>`) whenever
   a `Kconfig` file, a defconfig, or `CMakePresets.json` changed. A build
   directory keeps its own `kconfig/.config`, and a defconfig only seeds a
   directory that has none, so an existing directory can pass on a
   configuration the change no longer produces.

3. **Format.** `tools/format.sh --check`; `tools/format.sh` rewrites in
   place. Done when the check prints nothing.

4. **Links.** `python3 .agents/skills/preflight/scripts/check_links.py`.
   Done at `0 broken`.

5. **Reconcile the ledgers.** The repository records measured facts in
   prose and in expected test output. Each kind below is a ledger: when the
   change touches what it counts, rewrite it from this run's output. Find
   the copies by searching for the old value.

   | Ledger | Usually in | Changes when |
   |---|---|---|
   | Test totals and skipped counts | `docs/build.md`, "What is verified" | Any test is added, removed, or starts or stops skipping |
   | Contract tests run and skipped per implementation | The interface's module `README.md`; each backend's `PORT.md` | A contract suite or a fixture's capabilities change |
   | Test summaries an integration case expects | `tests/integration/integration.py` | A contract suite gains or loses a test |
   | Library code size | `docs/build.md`, "Code size"; measure with `.agents/skills/preflight/scripts/code_size.sh` | Library sources change |
   | The verified matrix, its date and tool versions | `docs/build.md`, "What is verified" | A check is added, dropped, or run on something new |
   | Allowed undefined symbols | `tests/cross/allowed-undefined.txt` | The symbol check reports a new symbol. Each entry carries its reason |
   | Public interface changes | `CHANGELOG.md` | Any public header changes, at any stability level |
   | Module and option tables | `README.md`; `docs/build.md`, "The options" | A module, backend, or Kconfig symbol is added or removed |
   | Milestone checkboxes | `docs/roadmap.md` | A deliverable is met |

   A decision that would otherwise be reargued gets a new record in
   `docs/adr/` (shape and index: `docs/adr/README.md`). An accepted record
   is superseded by a new one.

6. **Report in verification levels.** The vocabulary is fixed in
   `docs/build.md`, "Verification levels". For each claim, name the level
   reached and what it ran on. List what was not run under its own heading.
   A cross build that compiles is a compile check, and the report says so.
