#!/usr/bin/env python3
"""Stage immutable platform sources/loader, never read donor implementation HEAD.

Optional --cache is read-only. Every cached/downloaded archive must match the
published project pin before extraction. Files are written only below work/.
"""
import argparse, hashlib, io, json, pathlib, tarfile, urllib.request, zipfile, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
PINS = {
    'sdl3_image': ('https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.0/SDL3_image-3.4.0.tar.gz',
                   '2ceb75eab4235c2c7e93dafc3ef3268ad368ca5de40892bf8cffdd510f29d9d8'),
    'openal_soft': ('https://github.com/kcat/openal-soft/archive/refs/tags/1.24.2.tar.gz',
                    '7efd383d70508587fbc146e4c508771a2235a5fc8ae05bf6fe721c20a348bd7c'),
    'gamespy': ('https://github.com/TheAssemblyArmada/GamespySDK/archive/07e3d15c500415abc281efb74322ab6d9c857eb8.tar.gz',
                '14236ca0cccbbfd2ebae49da436357986b5f81afa2c01b6a9eb81f47ddaefe8b'),
    'lzhl': ('https://github.com/TheSuperHackers/lzhl-1.0/archive/dfd96e2ca64adaddb35dd4ebadd6add7d5586783.tar.gz',
             '8e1a491425dcffac040445fb06f2eb1c274d49e5f10985111b8dcda8e3482d6f'),
    'openxr': ('https://github.com/KhronosGroup/OpenXR-SDK-Source/archive/refs/tags/release-1.1.59.1.tar.gz',
               'b436aa008c51a09de06d15c470b26b3171835fcf52fc6de1631d4aa997b0f2ac'),
    'openxr-loader': ('https://repo.maven.apache.org/maven2/org/khronos/openxr/openxr_loader_for_android/1.1.59.1/openxr_loader_for_android-1.1.59.1.aar',
                      '38510c39c853532aebd7664aebe2768ec324e9aa189644081dedc272cbece77d'),
    'sdl3': ('https://github.com/libsdl-org/SDL/releases/download/release-3.4.2/SDL3-3.4.2.tar.gz',
             'ef39a2e3f9a8a78296c40da701967dd1b0d0d6e267e483863ce70f8a03b4050c'),
}

def extract(name, data, destination):
    destination.mkdir(parents=True, exist_ok=True)
    def target(relative):
        path = (destination / relative).resolve()
        if not path.is_relative_to(destination.resolve()):
            raise ValueError('Archive escapes dependency directory')
        path.parent.mkdir(parents=True, exist_ok=True)
        return path
    if name == 'openxr-loader':
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for member in archive.infolist():
                if not member.is_dir(): target(member.filename).write_bytes(archive.read(member))
    else:
        with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
            for member in archive.getmembers():
                if member.isfile():
                    relative = pathlib.PurePosixPath(member.name).parts[1:]
                    if relative: target(pathlib.Path(*relative)).write_bytes(archive.extractfile(member).read())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=pathlib.Path)
    args = parser.parse_args()
    record = {}
    for name, (url, expected) in PINS.items():
        archive = ROOT / 'work/downloads' / ('quest-' + name + ('.aar' if name.endswith('loader') else '.tar.gz'))
        archive.parent.mkdir(parents=True, exist_ok=True)
        data = archive.read_bytes() if archive.exists() else None
        if data is None and args.cache:
            for candidate in args.cache.iterdir():
                if candidate.is_file() and (name.replace('_', '-') in candidate.name.lower() or name in candidate.name.lower() or (name == 'sdl3' and 'sdl3' in candidate.name.lower())):
                    blob = candidate.read_bytes()
                    if hashlib.sha256(blob).hexdigest() == expected:
                        data = blob
                        break
        if data is None:
            with urllib.request.urlopen(url, timeout=120) as source: data = source.read()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected: raise SystemExit(f'{name}: hash mismatch: {actual}; refusing extraction')
        if not archive.exists(): archive.write_bytes(data)
        extract(name, data, ROOT / 'work/dependencies' / name)
        record[name] = {'url': url, 'sha256': actual}
        if name == 'openxr':
            sdk = ROOT / 'work/dependencies/openxr'
            generated = {}
            for header in ('openxr.h', 'openxr_platform.h'):
                subprocess.run([sys.executable, str(sdk / 'specification/scripts/genxr.py'),
                                '-registry', str(sdk / 'specification/registry/xr.xml'),
                                '-o', str(sdk / 'include/openxr'), header], check=True)
                generated[header] = hashlib.sha256((sdk / 'include/openxr' / header).read_bytes()).hexdigest()
            record[name]['generatedHeaders'] = generated
        print('Verified', name, actual)
    output = ROOT / 'work/evidence/platform-dependencies.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')

if __name__ == '__main__': main()
