#!/usr/bin/env python3
"""Build the upstream Android preset in isolated trees; never install or publish.

Tool paths are supplied in an ignored JSON file, not committed machine paths.
Run prepare-dependencies.py first. Control source must be a separate, unmodified
checkout of the pinned upstream commit. Dependency installation is explicit.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]


def run(command, cwd, log):
    print('Running:', subprocess.list2cmdline(list(map(str, command))), flush=True)
    with log.open('w', encoding='utf-8') as output:
        result = subprocess.run(list(map(str, command)), cwd=cwd,
                                stdout=output, stderr=subprocess.STDOUT)
    print('Exit:', result.returncode, 'Log:', log, flush=True)
    if result.returncode:
        print(log.read_text(encoding='utf-8', errors='replace')[-8000:])
        raise SystemExit(result.returncode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools', required=True, type=pathlib.Path)
    parser.add_argument('--profile', required=True, choices=('control', 'quest'))
    parser.add_argument('--hz', type=int, choices=(30, 60), default=60)
    parser.add_argument('--source', type=pathlib.Path)
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--public-build', action='store_true',
                        help='Map diagnostic paths and require a neutral dependency build')
    args = parser.parse_args()
    tools = json.loads(args.tools.read_text(encoding='utf-8'))
    # GeneralsX @build Codex 09/10/2026 Reuse verified dependencies while isolating source/build outputs.
    dependency_root = pathlib.Path(tools.get('dependencyRoot', ROOT))
    source = (args.source or ROOT).resolve()
    if args.profile == 'control' and source == ROOT:
        raise SystemExit('Use a separate pristine upstream source for control')
    source_pin = subprocess.check_output([tools['git'], '-C', str(source),
                                         'rev-parse', 'HEAD'], text=True).strip()
    if args.profile == 'control':
        expected = json.loads((ROOT / 'docs/WORKDIR/quest/UPSTREAM_PIN.json').read_text())['commit']
        if source_pin != expected:
            raise SystemExit('Control source does not match the selected upstream pin')
        changed = subprocess.check_output([tools['git'], '-C', str(source), 'diff',
                                           '--name-only', '--ignore-submodules'], text=True)
        if changed.strip():
            raise SystemExit('Control engine source is modified: ' + changed)
    build = ROOT / 'work/build' / f'{args.profile}-{args.hz}'
    build.mkdir(parents=True, exist_ok=True)
    evidence = ROOT / 'work/evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    prefix = f'{args.profile}-{args.hz}-{stamp}'
    temporary = ROOT / 'work/tmp' / f'{args.profile}-{args.hz}'
    temporary.mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(temporary), TMP=str(temporary))
    ndk = pathlib.Path(tools['ndk']).resolve()
    cmake = pathlib.Path(tools['cmake']).resolve()
    os.environ['ANDROID_NDK_HOME'] = str(ndk)
    os.environ['VCPKG_ROOT'] = str(dependency_root / 'work/vcpkg')
    os.environ['VCPKG_DOWNLOADS'] = str(dependency_root / 'work/downloads')
    os.environ['VCPKG_DISABLE_METRICS'] = '1'
    os.environ['GIT_EXEC_PATH'] = tools['gitExecPath']
    os.environ['PATH'] = os.pathsep.join(tools['path']) + os.pathsep + os.environ['PATH']
    os.environ['NINJA'] = tools['ninja']
    dependencies = json.loads((evidence / 'platform-dependencies.json').read_text())
    command = [cmake, '--preset', 'android-vulkan', '-S', source, '-B', build,
               '-UFFMPEG*', '-Upkgcfg_lib_FFMPEG*', '-U_pkg_config_checked_FFMPEG',
               '-DRTS_BUILD_OPTION_FFMPEG=ON',
               '-DCMAKE_MAKE_PROGRAM=' + tools['ninja'],
               '-DVCPKG_INSTALLED_DIR=' + str(dependency_root / 'work/control-deps'),
               '-DVCPKG_MANIFEST_INSTALL=OFF', '-DVCPKG_HOST_TRIPLET=x64-mingw-static',
               '-DMESON_EXECUTABLE=' + tools['meson'],
               '-DNINJA_EXECUTABLE=' + tools['ninja'],
               '-DSAGE_HIGH_FPS_SIM=' + ('ON' if args.hz == 60 else 'OFF')]
    # GeneralsX @build Codex 09/10/2026 Keep local identities out of release diagnostics.
    privacy_flags = []
    if args.public_build:
        if any(part.lower() in ('users', 'home') for part in dependency_root.parts):
            raise SystemExit('Public builds require a neutral dependency root, not a user directory')
        for old, new in tools.get('privacyPrefixMaps', []):
            for spelling in (old.replace('\\', '/'), old.replace('/', '\\')):
                privacy_flags.append('-ffile-prefix-map=' + spelling + '=' + new)
        privacy_flags.append('-fdebug-compilation-dir=/build')
        command += ['-DCMAKE_PROJECT_INCLUDE=' + str(ROOT / 'scripts/tooling/quest/public-path-map.cmake'),
                    '-DQUEST_PUBLIC_PATH_FLAGS=' + ';'.join(privacy_flags)]
    if args.profile == 'quest':
        command += ['-DSAGE_USE_OPENXR=ON',
                    '-DQUEST_OPENXR_ROOT=' + str(dependency_root / 'work/dependencies/openxr'),
                    '-DQUEST_OPENXR_LOADER=' + str(dependency_root / 'work/dependencies/openxr-loader/jni/arm64-v8a/libopenxr_loader.so')]
    # Every staged source was verified against upstream's actual archive pin.
    # SDL3's expected _deps location is also consumed by DXVK's pkg-config file.
    for name in ('sdl3', 'sdl3_image', 'openal_soft', 'gamespy', 'lzhl'):
        staged = build / '_deps' / (name + '-src')
        if not staged.exists():
            shutil.copytree(dependency_root / 'work/dependencies' / name, staged)
        if name == 'lzhl' and not (staged / 'CompLibHeader').exists():
            # The archive preparer exports this subdirectory's contents.
            # Preserve the upstream include spelling CompLibHeader/lzhl.h.
            shutil.copytree(dependency_root / 'work/dependencies' / name, staged / 'CompLibHeader')
        command += ['-DFETCHCONTENT_SOURCE_DIR_' + name.upper() + '=' + str(staged)]
    if 'adrenotools' in tools:
        command += ['-DFETCHCONTENT_SOURCE_DIR_ADRENOTOOLS=' + tools['adrenotools']]
    # The lzhl override names CompLibHeader itself, not the archive root.
    for i, flag in enumerate(command):
        if str(flag).startswith('-DFETCHCONTENT_SOURCE_DIR_LZHL='):
            candidate = build / '_deps/lzhl-src/CompLibHeader'
            if candidate.is_dir(): command[i] = '-DFETCHCONTENT_SOURCE_DIR_LZHL=' + str(candidate)
    record = {'profile': args.profile, 'simulationHz': args.hz, 'source': source_pin,
              'configuredAtUtc': stamp, 'tools': tools, 'dependencies': dependencies,
              'configureCommand': list(map(str, command)), 'status': 'NOT_BUILT',
              'runtime': 'NOT_VERIFIED', 'crossplay': 'NOT_VERIFIED',
              'appVersion': json.loads((ROOT / 'docs/WORKDIR/quest/APP_VERSION.json').read_text()) if args.profile == 'quest' else None}
    record['publicBuild'] = args.public_build
    manifest = evidence / (prefix + '.json')
    manifest.write_text(json.dumps(record, indent=2), encoding='utf-8')
    run(command, source, evidence / (prefix + '-configure.log'))
    if args.public_build:
        compile_records = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
        missing = [entry for entry in compile_records
                   if entry['file'].lower().endswith(('.c', '.cc', '.cpp', '.cxx'))
                   and any(flag not in entry['command'] for flag in privacy_flags if '\\' not in flag)]
        if missing: raise SystemExit('Public diagnostic path mapping is missing from actual compiler commands')
        record['actualCompilerPrivacyFlags'] = 'PASS'
    # Upstream's Linux/macOS cross template lacks Windows executable suffixes.
    # Adapt only this generated artifact for the pristine control source.
    if os.name == 'nt':
        cross = build / 'meson-arm64-android-cross.ini'
        text = cross.read_text(encoding='utf-8')
        for old, new in (('-clang\'', '-clang.cmd\''), ('-clang++\'', '-clang++.cmd\''),
                         ('/llvm-ar\'', '/llvm-ar.exe\''), ('/llvm-strip\'', '/llvm-strip.exe\'')):
            text = text.replace(old, new)
        for program in ('pkg-config', 'touch', 'glslang'):
            if program == 'touch':
                touch_script = ROOT / 'scripts/tooling/quest/host-adapters/touch.py'
                entry = "touch = ['" + pathlib.Path(sys.executable).as_posix() + "', '" + touch_script.as_posix() + "']"
                text = text.replace('[host_machine]', entry + '\n\n[host_machine]')
                continue
            executable = shutil.which(program)
            if not executable: raise SystemExit('Missing host program: ' + program)
            entry = program + " = '" + pathlib.Path(executable).as_posix() + "'"
            if program == 'pkg-config':
                text = text.replace("pkg-config = 'pkg-config'", entry)
            else:
                text = text.replace('[host_machine]', entry + '\n\n[host_machine]')
        # Explicit NDK built-in headers: Windows wrapper discovery can return a relative resource path.
        resource = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/lib/clang/18'
        if not (resource / 'include/stdarg.h').is_file():
            raise SystemExit('Pinned NDK built-in headers missing')
        resource_arg = '-resource-dir=' + resource.as_posix()
        text = text.replace("c_args = [", "c_args = ['" + resource_arg + "', ", 1)
        text = text.replace("cpp_args = [", "cpp_args = ['" + resource_arg + "', ", 1)
        if privacy_flags:
            mapped = ', '.join("'" + flag.replace('\\', '\\\\') + "'" for flag in privacy_flags) + ', '
            text = text.replace('c_args = [', 'c_args = [' + mapped, 1)
            text = text.replace('cpp_args = [', 'cpp_args = [' + mapped, 1)
        text = text.replace("c_link_args = [", "c_link_args = ['" + resource_arg + "', ", 1)
        text = text.replace("cpp_link_args = [", "cpp_link_args = ['" + resource_arg + "', ", 1)
        cross.write_text(text, encoding='utf-8')
        record['hostAdaptation'] = 'Windows executable suffixes, host tool paths and explicit pinned NDK built-in-header path in generated Meson cross file only'
    if os.name == 'nt' and not args.configure_only:
        meson_build = build / '_deps/dxvk-build-android'
        ninja_file = meson_build / 'build.ninja'
        if ninja_file.exists():
            configured = ninja_file.read_text(encoding='utf-8')
            host_stamp = meson_build / 'quest-host-config-sha256.txt'
            host_sha = hashlib.sha256(cross.read_bytes()).hexdigest()
            if not host_stamp.exists() or host_stamp.read_text().strip() != host_sha:
                # Refresh only this generated DXVK build when an old host-tool configuration persists.
                if not meson_build.resolve().is_relative_to(build.resolve()):
                    raise SystemExit('Unsafe generated DXVK build path')
                run([tools['meson'], 'setup', '--wipe', meson_build, source / 'references/fbraz3-dxvk',
                     '--cross-file', cross, '-Ddxvk_native_wsi=sdl3', '--buildtype=release'],
                    source, evidence / (prefix + '-dxvk-host-adaptation.log'))
                host_stamp.write_text(host_sha + '\n', encoding='utf-8')
    record['status'] = 'CONFIGURED'
    manifest.write_text(json.dumps(record, indent=2), encoding='utf-8')
    if args.configure_only: return
    run([cmake, '--build', build, '--parallel', str(args.jobs), '--target',
         'z_generals', 'dxvk_d3d8_install', 'main_hook', 'file_redirect_hook',
         'gsl_alloc_hook', 'hook_impl'], source, evidence / (prefix + '-build.log'))
    binaries = {}
    for name in ('libmain.so', 'libdxvk_d3d8.so', 'libdxvk_d3d9.so'):
        files = [p for p in build.rglob(name) if '_deps' not in p.relative_to(build).parts]
        if len(files) != 1: raise SystemExit('Missing/ambiguous built library: ' + name)
        binary = files[0]
        readelf = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
        header = subprocess.check_output([str(readelf), '-h', str(binary)], text=True)
        if 'AArch64' not in header: raise SystemExit('Wrong architecture: ' + name)
        binaries[name] = {'path': str(binary), 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
    record.update(status='BUILT', binaries=binaries)
    manifest.write_text(json.dumps(record, indent=2), encoding='utf-8')
    print('Native build verified as AArch64. Runtime and crossplay remain NOT_VERIFIED.')


if __name__ == '__main__': main()
