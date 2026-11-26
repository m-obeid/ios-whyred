# Working on ios-whyred

- Read `README.md`, then `whyred/research/recon/whyred-facts.md` before starting. The facts file
  holds the hardware facts, the bring-up log, CI run IDs, image names and the next steps.
  `NOTES.md` is the older sweet log.
- The user switches agents frequently. Update `whyred/research/recon/whyred-facts.md`
  after each milestone and before stopping. Record commits, CI run IDs, exact
  build/boot results and unfinished work. Distinguish observations from guesses.
- Commit completed changes. Use `gh` for GitHub Actions kernel builds and logs.
  Local single-file cross-compiles are useful before spending a CI run.
- Device bring-up uses temporary `fastboot boot` images. Preserve the Android
  installation and partition backups. The user approved (2026-10-15) using
  internal UFS for iOS and said Android may be deleted: the iOS root goes
  into the existing `userdata` partition (no GPT changes). Keep bootloaders,
  recovery, modem, persist and the backups intact, and confirm each
  destructive step with the user before running it.
- Keep device dumps, serial numbers and downloaded Apple artifacts out of git.
