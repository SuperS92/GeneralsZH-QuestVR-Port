# Quick Match map selection

The service playlist describes maps with Windows-style path suffixes. Custom
Android maps are cached under absolute paths with forward slashes. The menu's
exact string lookup could therefore reject installed maps and leave no selected
maps when Play checks the playlist minimum.

The Quest menu first preserves the original lookup, then tries a slash-normalized
cache key. Preferences use the resolved metadata key for both loading and saving.
Rows retain their indices in the service's complete playlist: excluding a missing
map or one below the player minimum must not change another map's wire identity.
The minimum-map check, HTTP client, matchmaking protocol and simulation are intact.
Other platform builds retain the existing path. No retail window assets are shipped.

Host tests exhaust available/selected combinations, including missing leading and
middle maps and invalid rows. Source/cache comparison found all 41 current 1v1
custom maps installed under matching portable keys, versus no exact matches with
the mixed-separator paths. This establishes the lookup defect, not an end-to-end
matchmaking success. Playlist display and searching still need a Quest test.

The older spatial AA experiment is no longer exposed or linked into the Quest
engine. Stored preferences cannot reactivate it or prevent TAA. Off, Lite, Medium
and Full TAA remain available, with descriptions independent of retired controls.
