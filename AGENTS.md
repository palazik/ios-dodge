# Working on ios-dodge

This is palazik's **ios-dodge** — a fork of **dipa-studio/ios-sweet**, porting
iOS 16 (xnu-8796) to the **OnePlus 13 (dodge, Qualcomm SM8750 / Oryon)**. dipa's
original sweet work is kept intact as the method reference and as credit:
`xnu/patches/` (their ~85 ordered patches), `shim/`, `tools/`, and
`AGENTS-sweet-upstream.md`, `NOTES-sweet-upstream.md`, `README-sweet-upstream.md`.
Our work lives under `dodge/`.

Credit dipa-studio for the method. **Copy none of their code** — their repo has
no license; model on Apple's open APSL headers and write our own.

## Workflow
- Read **`NOTES.md`** (our dodge handoff) first. Read **`NOTES-sweet-upstream.md`**
  for dipa's full sweet bring-up log — the proven, step-by-step recipe that reached
  the SpringBoard home screen (HID, identity, touch, display, SMP, etc.), and their
  QEMU smoke-test flow (`tools/qemu-sweet.py`, per-kernel "QEMU twin" ADTs).
- Update `NOTES.md` after each milestone and before stopping: commits, CI run IDs,
  exact build/boot results, and unfinished work. Distinguish observations from guesses.
- Author xnu changes in the local checkout (`~/ios18_op13/work/ios16/xnu/`, git;
  commit as `palazik <gochmyradov0@gmail.com>`), export with `git format-patch`, and
  place the `.patch` files in `dodge/patches/`. A local single-file cross-compile
  beats spending a CI run.
- Kernel builds run on GitHub Actions (`.github/workflows/xnu.yml`). Dispatch:
  `gh workflow run xnu.yml -R palazik/ios-dodge -f kernel_config=DEVELOPMENT -f machine_config=DODGE -f keep_going=true`
  (fork `push` events do NOT auto-trigger). Push workflow files via SSH
  (`git@github.com:palazik/ios-dodge`) — the OAuth token lacks `workflow` scope.

## Device policy — follow THIS, not sweet's
The boot must be **NON-DESTRUCTIVE**. **Never wipe userdata; never delete Android.**
**Read `RECOVERY.md` before any on-device test** — a sibling port destroyed its
LUN0 GPT via the iOS UFS driver; RECOVERY.md has the never-do list, the GPT-backup
commands, and the safe boot-args (no `ufsrw`/`rootrw`).
- Bring-up uses temporary `fastboot boot <image>` (loaded into RAM, nothing flashed).
- If a flash is ever unavoidable, use the **inactive A/B slot** and switch back for
  Android. The iOS root is a FILE / ramdisk — never written over an Android partition.
- Keep bootloaders, recovery, modem, persist and any backups intact. Confirm every
  destructive step with the user first.
- NOTE: `AGENTS-sweet-upstream.md` allows deleting Android and putting iOS in the
  userdata partition — that was dipa's user's decision, NOT ours. Do not apply it here.
- ARB1 is not a boot blocker (Anti-Rollback only gates the Qualcomm signed firmware
  chain XBL/ABL/TZ, not the custom boot image an unlocked ABL loads). Never flash or
  downgrade official firmware.

## Hygiene
Keep device dumps, serial numbers and downloaded Apple artifacts out of git.

## Commit messages

Conventional-commit prefixes: `feat:`, `fix:`, `docs:`, `refactor:`, `chore:`,
`build:`, `test:`. Not `dodge:` — this repository only targets one device, so
naming it in every subject line says nothing. Patches 0001-0040 predate this
and are left alone rather than rewritten.

Use a scope only when it narrows something real, e.g. `fix(shim):`,
`feat(ufs):`. The body should say why, and distinguish what was measured on the
device from what was inferred.
