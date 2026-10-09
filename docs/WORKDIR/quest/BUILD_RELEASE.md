# Building the Quest APK

The local build uses Windows, Python 3, Git, NDK 27.1.12297006, CMake 3.31.6,
Ninja, Meson, JDK 17, Gradle 8.7 and Android SDK/build tools 35. The minimum
Android API remains 28. No retail game files are needed to compile.

Clone with submodules. Keep tool paths in ignored `work/build-tools.json`;
the scripts document its required fields. Use separate source/build trees
for the pristine MYSOREZ control and Quest. UPSTREAM_PIN.json records commits;
prepare-dependencies.py checks the platform archives and OpenXR loader hashes.

```powershell
python scripts/tooling/quest/prepare-dependencies.py
python scripts/tooling/quest/prepare-windows-host.py --tools work/build-tools.json
```

Provision the pinned vcpkg tool/baseline with the unchanged upstream manifest,
Android triplet and port overlay. The explicit host overlay fixes Windows host
staging/protoc paths, not Android protobuf or codec algorithms. Install
arm64-android and x64-mingw-static dependencies to work/control-deps. Retain all
manifest features, including FFmpeg and GameNetworkingSockets ICE. MinGW protoc
needs its compiler runtime DLLs beside the host executable.

```powershell
python scripts/build/quest/build-windows.py --tools work/build-tools.json --profile quest --hz 60
python scripts/build/quest/build-windows.py --tools work/build-tools.json --profile quest --hz 30
```

Both variants must use the same clean source commit and APP_VERSION.json. Private
build manifests go under work/evidence. Build the pristine control with
`--profile control --source CONTROL_CHECKOUT --hz 60` at the pinned upstream.

## Public build privacy

Build dependencies in a neutral path such as C:/GeneralsQuestReleaseBuild,
without an account name. The ignored tools JSON accepts privacyPrefixMaps as
pairs of original path and generic replacement. --public-build maps diagnostic
paths for CMake and Meson without changing numeric options.

Provision public dependencies using scripts/build/quest/triplets before the
ordinary upstream triplets. Set QUEST_PUBLIC_PREFIX_FLAGS to the same compiler
maps. Rebuild static libraries too: cached libraries can retain source paths
and generated configuration strings. Do not reuse private static builds for
publication or patch bytes in a finished binary.

```powershell
python scripts/build/quest/build-windows.py --tools work/build-tools.json --profile quest --hz 60 --public-build
python scripts/build/quest/build-windows.py --tools work/build-tools.json --profile quest --hz 30 --public-build
python scripts/build/quest/package-windows.py --tools work/build-tools.json --engine30 MANIFEST30 --engine60 MANIFEST60 --signing-config PRIVATE_SIGNING_JSON
```

Public early-release APKs retain the tested development build settings and use
a private Quest signing key. Never commit the key, password or signing JSON.
Keep an encrypted backup for future updates. Existing test installs need a
planned data backup/migration when changing from the public debug key.

Use PKCS12 with the same store and key password; the ignored password file's
first line supplies it. Packaging explicitly enables v2 signing and verifies
the expected certificate. It may reuse unchanged native engines after a
packaging or Markdown-only follow-up commit. The manifest records native and
packaging source commits separately; engine, launcher or build-input changes
require new native builds.

Packaging checks both source/version/hash manifests, ARM64 dependency closure,
signature, package identity, ZIP integrity and alignment. DXVK retains symbols
because upstream stripping caused runtime failures. The optional --ecj adapter
uses a pinned host compiler absent from the APK. Full manifests and artifacts
stay under ignored releases/.

## Before uploading

Run check-boundary.py with the sealed offline-v0.44 source and compare actual
native commands with check-build-profile.py. Audit the exact public ref and
final APK using audit-publication.py and an ignored local deny-file. Review
findings, notices, corresponding sources and exact upload refs/assets manually.

Publish the engine source/tag, matching dependency sources and license notices
with the APK. Private manifests, symbols, logs, replays, accounts, signing
material and game files are not release assets. These build and pattern checks
do not certify headset behavior or PC crossplay; state the actual runtime tests.
