#!/usr/bin/env python3
"""Prepare explicit host-only overlays against immutable vcpkg port trees."""
import argparse
import hashlib
import io
import json
import pathlib
import shutil
import subprocess
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
ADAPTERS = pathlib.Path(__file__).with_name('host-adapters')
PORTS = {'ffmpeg': '3eec7480c1d2f5b777715b974c2935e5a57b06df',
         'protobuf': '53c97b927087d1919f2a7ef6c34e4caf1a00cdf9',
         'openssl': 'd740be74dc7940c2b88e69db0b2d181024e1028c'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools', required=True, type=pathlib.Path)
    args = parser.parse_args()
    tools = json.loads(args.tools.read_text(encoding='utf-8'))
    overlay = ROOT / 'work/host-overlay'
    evidence = {}
    for name, tree in PORTS.items():
        destination = overlay / name
        patch = ADAPTERS / (name + '-port.patch')
        if not destination.exists():
            archive = subprocess.check_output([tools['git'], '-C', str(ROOT / 'work/vcpkg'),
                                                'archive', '--format=tar', tree])
            destination.mkdir(parents=True)
            with tarfile.open(fileobj=io.BytesIO(archive)) as files:
                for member in files:
                    target = (destination / member.name).resolve()
                    if not target.is_relative_to(destination.resolve()) or not member.isfile():
                        if member.isdir(): continue
                        raise SystemExit('Unexpected port archive member')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(files.extractfile(member).read())
        # GeneralsX @build Codex 09/10/2026 Normalize only generated host-overlay text before patching.
        portfile = destination / 'portfile.cmake'
        portfile.write_bytes(portfile.read_bytes().replace(b'\r\n', b'\n'))
        command = [tools['git'], '-C', str(destination), 'apply', '--no-index']
        if subprocess.run(command + ['--reverse', '--check', str(patch)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            subprocess.run(command + ['--check', str(patch)], check=True)
            subprocess.run(command + [str(patch)], check=True)
        evidence[name] = {'portTree': tree, 'patchSha256': hashlib.sha256(patch.read_bytes()).hexdigest()}
    shutil.copyfile(ADAPTERS / 'quest-mingw-protoc.patch', overlay / 'protobuf/quest-mingw-protoc.patch')
    shutil.copyfile(ADAPTERS / 'openssl-metadata-paths.patch', overlay / 'openssl/openssl-metadata-paths.patch')
    binary = overlay / 'bin'
    binary.mkdir(exist_ok=True)
    shutil.copyfile(ADAPTERS / 'scoped-mkdir.py', binary / 'mkdir.py')
    launcher = '#!/usr/bin/env bash\nexec "' + pathlib.Path(sys.executable).as_posix() + '" "' + (binary / 'mkdir.py').as_posix() + '" "$@"\n'
    (binary / 'mkdir').write_bytes(launcher.encode())
    # MinGW protoc needs its host compiler runtime; this never enters the APK.
    protoc = ROOT / 'work/control-deps/x64-mingw-static/tools/protobuf'
    if protoc.exists():
        runtime = pathlib.Path(tools['hostCxxRuntime'])
        for name in ('libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll'):
            shutil.copyfile(runtime / name, protoc / name)
    (ROOT / 'work/evidence/windows-host-adapters.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print('Pinned host overlays prepared. Android protobuf/network sources are unchanged.')


if __name__ == '__main__': main()
