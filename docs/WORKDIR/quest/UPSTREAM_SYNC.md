# Updating from MYSOREZ

`upstream` is MYSOREZ/GeneralsZH-Android-Port. `origin` is this Quest fork.
Keep public history intact and merge explicit upstream releases after review.
A GitHub fork preserves the connection; it does not automatically adapt Quest hooks.

1. Keep the previous release, source tag and private build evidence for rollback.
2. Fetch upstream and choose an exact release tag or commit. Read its changes,
   AGENTS instructions and crossplay notes, including changed dependency pins.
3. Start a temporary update branch from the current Quest branch:

   ```sh
   git fetch upstream --tags
   git switch -c update/mysorez-VERSION
   git merge --no-commit --no-ff UPSTREAM_TAG_OR_COMMIT
   ```

4. Review conflicts individually. Check every Quest hook in PATCH_LEDGER.json;
   never resolve the entire merge with ours/theirs. Update UPSTREAM_PIN.json
   and the ledger to the reviewed upstream commit. Keep the offline-v0.44 pin.
5. Build the pristine upstream control and both Quest simulation variants in
   separate source/build trees. Check numeric flags, dependency ABI, input,
   stereo, campaign transitions and replay compatibility. Run current upstream
   checks as well as the Quest checks. CRC source pins require explicit review.
6. Test skirmish and a complete PC crossplay match. Keep private logs/replays,
   compare non-empty CRC checkpoints, and record any first divergence.
7. After the maintainer approves the tested candidate, finish the merge, tag
   a new release, sign with the existing private release key and publish the
   scanned APK. Until then, keep the current public release unchanged.

Online-data updates in the launcher are separate from engine/APK updates.
They retain upstream checksum validation and PC compatibility selection.
Never bypass validation or assume every future PC client is compatible.

Published updates should be ordinary merges, not force-pushes. Keep the changes
in optional Quest modules where possible; upstream updates can still need work.
