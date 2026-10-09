#!/usr/bin/env python3
"""Verify both native engine manifests and package an immutable Quest dev APK.

Does not install, launch, clear data, publish, or include retail game archives.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
FONTS_SHA = '7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0'
ECJ_SHA = 'c8f5e66787ea6e4e0394ba3051c86a4edc50683ce9850f71739646529a99a2d7'
SYSTEM = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so',
          'libvulkan.so', 'libEGL.so', 'libGLESv1_CM.so', 'libGLESv2.so',
          'libGLESv3.so', 'libjnigraphics.so', 'libOpenSLES.so', 'libz.so',
          'libmediandk.so', 'libaaudio.so', 'libnativewindow.so'}


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch(url, destination, expected):
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=120) as response:
            destination.write_bytes(response.read())
    if sha(destination) != expected: raise SystemExit('Dependency hash mismatch: ' + str(destination))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools', required=True, type=pathlib.Path)
    parser.add_argument('--engine30', required=True, type=pathlib.Path)
    parser.add_argument('--engine60', required=True, type=pathlib.Path)
    parser.add_argument('--ecj', action='store_true', help='Use pinned Windows host compiler adapter')
    parser.add_argument('--signing-config', type=pathlib.Path,
                        help='Ignored local JSON with keystore, password-file, alias and certificate digest')
    args = parser.parse_args()
    tools = json.loads(args.tools.read_text(encoding='utf-8'))
    # GeneralsX @build Codex 09/10/2026 Reuse verified dependencies while isolating source/build outputs.
    dependency_root = pathlib.Path(tools.get('dependencyRoot', ROOT))
    version = json.loads((ROOT / 'docs/WORKDIR/quest/APP_VERSION.json').read_text(encoding='utf-8'))
    temporary = ROOT / 'work/tmp/package'
    temporary.mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(temporary), TMP=str(temporary))
    records = {}
    for hz, manifest in ((30, args.engine30), (60, args.engine60)):
        record = json.loads(manifest.read_text(encoding='utf-8'))
        if record['status'] != 'BUILT' or record['profile'] != 'quest' or record['simulationHz'] != hz:
            raise SystemExit('Wrong/unbuilt engine manifest')
        for binary in record['binaries'].values():
            if sha(pathlib.Path(binary['path'])) != binary['sha256']:
                raise SystemExit('Engine manifest hash mismatch')
        if record.get('appVersion') != version:
            raise SystemExit('Native engine and APK display versions differ; rebuild both engines')
        records[hz] = record
    if args.signing_config and not all(r.get('publicBuild') for r in records.values()):
        raise SystemExit('Public signing requires both neutral-path native builds')
    if records[30]['source'] != records[60]['source']:
        raise SystemExit('Both native engines must originate from the same source commit')
    build = ROOT / 'work/build/quest-60'
    libraries = {}
    for path in build.rglob('*.so'):
        if path.name.startswith('lib') and '-src' not in '/'.join(path.parts):
            if path.name not in libraries: libraries[path.name] = path
    for name in ('libdxvk_d3d8.so', 'libdxvk_d3d9.so'):
        libraries[name] = pathlib.Path(records[60]['binaries'][name]['path'])
        if records[30]['binaries'][name]['sha256'] != records[60]['binaries'][name]['sha256']:
            raise SystemExit('30/60 shared DXVK builds differ; review before packaging')
    libraries['libmain.so'] = pathlib.Path(records[30]['binaries']['libmain.so']['path'])
    libraries['libmain60.so'] = pathlib.Path(records[60]['binaries']['libmain.so']['path'])
    ndk = pathlib.Path(tools['ndk'])
    native = ndk / 'toolchains/llvm/prebuilt/windows-x86_64'
    libraries['libc++_shared.so'] = native / 'sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
    libraries['libopenxr_loader.so'] = dependency_root / 'work/dependencies/openxr-loader/jni/arm64-v8a/libopenxr_loader.so'
    required = {'libmain.so', 'libmain60.so', 'libSDL3.so', 'libSDL3_image.so',
                'libopenal.so', 'libgamespy.so', 'libadrenotools.so',
                'libdxvk_d3d8.so', 'libdxvk_d3d9.so', 'libopenxr_loader.so', 'libc++_shared.so',
                'libmain_hook.so', 'libfile_redirect_hook.so', 'libgsl_alloc_hook.so', 'libhook_impl.so'}
    closure = {}
    queue = list(required)
    while queue:
        name = queue.pop()
        if name in closure: continue
        if name not in libraries or not libraries[name].is_file(): raise SystemExit('Missing runtime library: ' + name)
        binary = libraries[name]
        header = subprocess.check_output([str(native / 'bin/llvm-readelf.exe'), '-h', str(binary)], text=True)
        if 'AArch64' not in header: raise SystemExit('Wrong library architecture: ' + name)
        dynamic = subprocess.check_output([str(native / 'bin/llvm-readelf.exe'), '-d', str(binary)], text=True)
        needed = re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic)
        closure[name] = {'sha256': sha(binary), 'needed': needed}
        queue.extend(dep for dep in needed if dep not in SYSTEM)
    stage = ROOT / 'android/app/src/main/jniLibs/arm64-v8a'
    stage.mkdir(parents=True, exist_ok=True)
    # Only our generated stage files are replaced; fail on foreign content.
    for path in stage.iterdir():
        if not path.is_file() or path.name not in closure: raise SystemExit('Unexpected JNI stage file: ' + str(path))
    for name in closure: shutil.copyfile(libraries[name], stage / name)
    java = ROOT / 'android/app/src/main/java-sdl'
    if not java.exists(): shutil.copytree(dependency_root / 'work/dependencies/sdl3/android-project/app/src/main/java', java)
    for patch in (ROOT / 'android/patches').glob('*.patch'):
        command = [tools['git'], '-C', str(ROOT), 'apply', '--directory=android/app/src/main/java-sdl']
        if subprocess.run(command + ['--reverse', '--check', str(patch)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            subprocess.run(command + ['--check', str(patch)], check=True)
            subprocess.run(command + [str(patch)], check=True)
    assets = ROOT / 'android/app/src/main/assets/gamedata'
    fonts = assets / 'fonts'
    fonts.mkdir(parents=True, exist_ok=True)
    archive = dependency_root / 'work/downloads/liberation-fonts-ttf-2.1.5.tar.gz'
    fetch('https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz', archive, FONTS_SHA)
    names = {'LiberationSans-Regular.ttf': 'arial.ttf', 'LiberationSans-Bold.ttf': 'arialbold.ttf',
             'LiberationMono-Regular.ttf': 'couriernew.ttf', 'LiberationSerif-Regular.ttf': 'timesnewroman.ttf'}
    copied = set()
    with tarfile.open(archive) as members:
        for member in members:
            name = pathlib.PurePosixPath(member.name).name
            if member.isfile() and (name in names or name in ('LICENSE', 'OFL.txt')):
                (fonts / names.get(name, name)).write_bytes(members.extractfile(member).read())
                copied.add(name)
    if not names.keys() <= copied or not copied & {'LICENSE', 'OFL.txt'}: raise SystemExit('Missing fonts/license')
    # Match the proven offline-v0.44 Quest texture-filtering baseline. This
    # changes sampling only, never game LOD/INI data or network simulation.
    (assets / 'dxvk.conf').write_text(
        'd3d9.forceSamplerTypeSpecConstants = False\n'
        'd3d9.samplerAnisotropy = 16\n'
        'dxvk.enableAsync = false\n'
        'dxvk.numCompilerThreads = 4\n', encoding='utf-8')
    # No game LOD/INI override is bundled: device performance and replay assets
    # must be measured before altering potentially logic-visible detail settings.
    ecj = dependency_root / 'work/dependencies/ecj/ecj-3.45.0.jar'
    command = [tools['gradle'], '--no-daemon', '--no-watch-fs', '--max-workers=2',
               '-Duser.home=' + str(ROOT / 'work/java-home'), '-PquestVR',
               '-PquestDependencyRoot=' + str(dependency_root),
               '-PquestAndroidBuildDir=' + str(pathlib.Path(tools.get('androidBuildRoot', ROOT / 'android/app/build'))),
               ':app:assembleQuestDebug']
    if args.ecj:
        fetch('https://repo.maven.apache.org/maven2/org/eclipse/jdt/ecj/3.45.0/ecj-3.45.0.jar', ecj, ECJ_SHA)
        command[1:1] = ['--init-script', str(pathlib.Path(__file__).with_name('ecj-windows.gradle'))]
    # Gradle's incremental APK update can retain obsolete compressed native
    # library payloads as unreferenced ZIP gaps. Recreate only this generated
    # APK so repeated Quest builds do not grow by ~96 MB per engine update.
    apk = pathlib.Path(tools.get('androidBuildRoot', ROOT / 'android/app/build')) / 'outputs/apk/quest/debug/app-quest-debug.apk'
    apk.unlink(missing_ok=True)
    os.environ.update(JAVA_HOME=tools['jdk'], ANDROID_HOME=tools['sdk'], ANDROID_SDK_ROOT=tools['sdk'],
                      GRADLE_USER_HOME=str(dependency_root / 'work/gradle-home'))
    os.environ['PATH'] = str(pathlib.Path(tools['jdk']) / 'bin') + os.pathsep + os.environ['PATH']
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    log = ROOT / 'work/evidence' / ('package-' + stamp + '.log')
    with log.open('w', encoding='utf-8') as output:
        result = subprocess.run(command, cwd=ROOT / 'android', stdout=output, stderr=subprocess.STDOUT)
    print('Gradle exit:', result.returncode, 'log:', log, flush=True)
    if result.returncode:
        print(log.read_text(encoding='utf-8', errors='replace')[-6000:]); raise SystemExit(result.returncode)
    # GeneralsX @build Codex 09/10/2026 Sign public development builds with a private local key.
    expected_certificate = '644a3b0f8234c6ea8960ece9b55a161c00966b8dd522d2558d84883f4f77c48e'
    signing_label = 'upstream committed public debug key'
    sdk_tools = pathlib.Path(tools['sdk']) / 'build-tools/35.0.0'
    if args.signing_config:
        signing = json.loads(args.signing_config.read_text(encoding='utf-8'))
        signed = apk.with_name('app-quest-private-signed.apk')
        subprocess.run([str(pathlib.Path(tools['jdk']) / 'bin/java.exe'), '-jar', str(sdk_tools / 'lib/apksigner.jar'),
                        'sign', '--ks', signing['keyStore'], '--ks-key-alias', signing['alias'],
                        '--ks-pass', 'file:' + signing['passwordFile'], '--key-pass', 'file:' + signing['passwordFile'],
                        '--out', str(signed), str(apk)], check=True)
        apk = signed
        expected_certificate = signing['certificateSha256']
        signing_label = 'private Quest release key; development build'
    with zipfile.ZipFile(apk) as package:
        if package.testzip() is not None: raise SystemExit('APK ZIP integrity failure')
        apk_libraries = {pathlib.PurePosixPath(name).name for name in package.namelist() if name.startswith('lib/arm64-v8a/')}
        if apk_libraries != set(closure): raise SystemExit('APK library set differs from verified closure')
        for name in package.namelist():
            if name.lower().endswith(('.big', '.rep')): raise SystemExit('Private/game file in APK')
        for name, entry in closure.items():
            # AGP may strip ordinary libs, but preserve exact DXVK bytes.
            if name.startswith('libdxvk_') and hashlib.sha256(package.read('lib/arm64-v8a/' + name)).hexdigest() != entry['sha256']:
                raise SystemExit('DXVK library was stripped/replaced')
            entry['apkSha256'] = hashlib.sha256(package.read('lib/arm64-v8a/' + name)).hexdigest()
    # GeneralsX @build Codex 09/10/2026 Validate the actual signed artifact before creating its installer evidence.
    sdk_tools = pathlib.Path(tools['sdk']) / 'build-tools/35.0.0'
    signature = subprocess.check_output([str(pathlib.Path(tools['jdk']) / 'bin/java.exe'),
        '-jar', str(sdk_tools / 'lib/apksigner.jar'), 'verify', '--verbose', '--print-certs', str(apk)], text=True)
    if 'Verified using v2 scheme (APK Signature Scheme v2): true' not in signature or \
        'Signer #1 certificate SHA-256 digest: ' + expected_certificate not in signature:
        raise SystemExit('APK signature differs from the existing Quest application')
    subprocess.run([str(sdk_tools / 'zipalign.exe'), '-c', '-P', '16', '4', str(apk)], check=True)
    badging = subprocess.check_output([str(sdk_tools / 'aapt.exe'), 'dump', 'badging', str(apk)], text=True, encoding='utf-8')
    if "package: name='com.generalsx.questvr' versionCode='" + str(version['versionCode']) + \
        "' versionName='" + version['versionName'] + "'" not in badging or "native-code: 'arm64-v8a'" not in badging:
        raise SystemExit('APK identity/version/architecture differs from the intended Quest artifact')
    release = ROOT / 'releases' / (stamp + '-' + sha(apk)[:12])
    release.mkdir(parents=True, exist_ok=False)
    apk_name = 'GeneralsZH-QuestVR-' + version['versionName'] + '.apk'
    shutil.copyfile(apk, release / apk_name)
    record = {'package': 'com.generalsx.questvr', 'versionName': version['versionName'], 'versionCode': version['versionCode'],
              'apkSha256': sha(apk), 'engines': records, 'libraries': closure,
              'fontsArchiveSha256': FONTS_SHA, 'javaCompiler': 'ECJ 3.45.0' if args.ecj else 'Javac',
              'gitCommit': subprocess.check_output([tools['git'], '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
              'runtime': 'NOT_VERIFIED', 'crossplay': 'NOT_VERIFIED', 'signing': signing_label,
              'artifactQA': {'signatureV2': 'PASS', 'certificateSha256': expected_certificate, 'zipIntegrity': 'PASS',
                             'zipAlignment16KiB': 'PASS', 'actualIdentityVersionArm64': 'PASS'}}
    (release / 'manifest.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    (release / 'SHA256SUMS.txt').write_text(sha(apk) + '  ' + apk_name + '\n', encoding='utf-8')
    print('Immutable APK:', release / apk_name)


if __name__ == '__main__': main()
