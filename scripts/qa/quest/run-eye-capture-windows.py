#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise real Vulkan readback and independently verify the lossless files."""
import argparse
import datetime
import json
import pathlib
import re
import shutil
import subprocess

root = pathlib.Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools', required=True, type=pathlib.Path)
args = parser.parse_args()
tools = json.loads(args.tools.read_text())
work = root / 'work/eye-capture-smoke'
work.mkdir(exist_ok=True)
sysroot = pathlib.Path(tools['ndk']) / 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for folder in ('vulkan', 'vk_video'):
    shutil.copytree(sysroot / folder, work / 'include' / folder, dirs_exist_ok=True)
source = root / 'GeneralsMD/Code/Main/QuestVR/QuestEyeCapture.cpp'
test = pathlib.Path(__file__).with_name('test-eye-capture.cpp')
aa = source.with_name('QuestSpatialAA.cpp')
smaa = source.with_name('QuestSMAA.cpp')
performance = source.with_name('QuestPerformance.cpp')
draw = source.with_name('QuestDrawCapture.cpp')
terrain = source.with_name('QuestTerrainDetailTest.cpp')
mips = source.with_name('QuestTerrainMipFilter.cpp')
choices = source.with_name('QuestGraphicsChoices.cpp')
cutouts = source.with_name('QuestCutoutMips.cpp')
soft = source.with_name('QuestSoftCutouts.cpp')
wind = source.with_name('QuestTreeWind.cpp')
contours = source.with_name('QuestTreeContours.cpp')
far_mips = source.with_name('QuestTerrainFarMips.cpp')
fences = source.with_name('QuestFenceContours.cpp')
functions = sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(', source.read_text() + aa.read_text() + smaa.read_text() + test.read_text())))
definition = work / 'vulkan.def'
definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n' + '\n'.join(functions) + '\n', encoding='utf-8')
compiler = pathlib.Path(tools['hostCxxRuntime'])
subprocess.run([str(compiler / 'llvm-dlltool.exe'), '-m', 'i386:x86-64', '-d', str(definition), '-l', str(work / 'libvulkan.a')], check=True)
exe = work / 'capture-smoke.exe'
subprocess.run([str(compiler / 'clang++.exe'), '-std=c++17', '-static', '-D__ANDROID__', '-DSAGE_USE_OPENXR', '-DQUEST_DRAW_CAPTURE_HOST', '-DQUEST_SOFT_CUTOUT_HOST', '-DQUEST_FENCE_CONTOUR_HOST',
    '-I' + str(work / 'include'), '-I' + str(root / 'scripts/qa/quest/host-shims'),
    '-I' + str(root / 'Core/GameEngineDevice/Include'), '-I' + str(source.parent),
    str(source), str(aa), str(smaa), str(performance), str(source.with_name('QuestShroudPass.cpp')), str(draw), str(terrain), str(mips), str(choices), str(cutouts), str(soft), str(wind), str(contours), str(far_mips), str(fences), str(test), str(work / 'libvulkan.a'), '-o', str(exe)], check=True)
sessions = work / datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
result = subprocess.run([str(exe), sessions.as_posix()], capture_output=True, text=True, timeout=60)
(work / 'result.log').write_text(result.stdout + result.stderr, encoding='utf-8')
print(result.stdout + result.stderr)
if result.returncode:
    raise SystemExit(result.returncode)
draw = json.loads((sessions / 'draw-fixture.json').read_text())
assert draw['eyes'][0]['draws'] == 2 and draw['eyes'][1]['draws'] == 1
assert [eye['states'][0]['count'] for eye in draw['eyes']] == [2, 1]
texture = draw['eyes'][0]['states'][0]['textures'][0]
assert texture['nameHint'] == 'fence"\\\n.dds'
assert texture['levels'] == 8 and texture['widths'][:8] == [128, 64, 32, 16, 8, 4, 2, 1]
assert texture['sampler'][3] == 1056964608 and texture['samplerValidMask'] == 511
bounds = json.loads((sessions / 'draw-bounds.json').read_text())['eyes']
assert bounds[0]['draws'] == 4100 and len(bounds[0]['states']) == 256
assert bounds[0]['omitted'] == 3844 and bounds[0]['truncated']
assert not bounds[1]['entered'] and not bounds[1]['states']
failed = json.loads((sessions / 'draw-failed-getters.json').read_text())['eyes'][1]['states'][0]
assert failed['renderValidMask'] == 0 and failed['textures'][0]['samplerValidMask'] == 0
config_text = (sessions / 'config-fixture.json').read_text()
assert 'SECRET' not in config_text and 'account' not in config_text
config = json.loads(config_text)
assert config['inputsOnlyNoRuntimeConfirmation'] and config['configFileReadable']
assert [entry['key'] for entry in config['values']] == ['d3d9.samplerLodBias', 'd3d9.samplerAnisotropy']
complete = []
for session in sorted((sessions / 'quest-eye-captures').iterdir()):
    if not (session / 'manifest.json').exists():
        continue
    manifest = json.loads((session / 'manifest.json').read_text())
    lines = [json.loads(line) for line in (session / 'frames.jsonl').read_text().splitlines()]
    if len(lines) < 8:
        assert len(lines) <= 1
        continue  # Intentional abort/writer-failure fixtures.
    assert len(lines) == 8
    assert [line['index'] for line in lines] == list(range(8))
    assert all(line['predictedDisplayTime'] == 123456789123 for line in lines)
    assert all(line['eyes'][0]['positionMeters'][0] < 0 < line['eyes'][1]['positionMeters'][0] for line in lines)
    assert all(all(e['cameraValid'] and e['worldUnitsPerMeter'] == 500 for e in line['eyes']) for line in lines)
    assert manifest['softCutoutsRequested'] == (len(complete) == 2)
    far_diagnostics = manifest['terrainFarMipDiagnostics']
    assert all(line['terrainFarMipDiagnostics'] == far_diagnostics for line in lines)
    if len(complete) == 2:
        assert far_diagnostics == {'schema':3,'requested':True,'edgeGuard':True,
            'attempts':0,'uploads':0,'rejected':0,'failures':0,'lastReject':0,'lastCellsChecked':0,
            'filterRequested':True,'filterUploads':0,'filterFailures':0,'filterMaterials':0,
            'filterSkippedAlpha':0,'filterChangedPixels':0,'filterUnequalEndpoints':0,'filterLastFailure':0}
    else:
        assert far_diagnostics is None
    fence_diagnostics = manifest['fenceContourDiagnostics']
    assert all(line['fenceContourDiagnostics'] == fence_diagnostics for line in lines)
    if len(complete) == 2:
        assert fence_diagnostics == {'schema':1,'requested':True,'uploads':0,'uploadFailures':0,'changedPixels':0,
            'lastPartialBefore':0,'lastPartialAfter':0,'eyeDraws':[1,0],'eyeFailures':[0,1],'eyeSkips':[0,1]}
    else:
        assert fence_diagnostics is None
    contour_diagnostics = manifest['treeContourDiagnostics']
    assert all(line['treeContourDiagnostics'] == contour_diagnostics for line in lines)
    if len(complete) == 2:
        assert contour_diagnostics == {'schema':1,'requested':True,'uploads':1,'failures':1,
            'regions':1,'changedPixels':9,'lastPartialBefore':0,'lastPartialAfter':9}
    else:
        assert contour_diagnostics is None
    wind_diagnostics = manifest['treeWindDiagnostics']
    assert all(line['treeWindDiagnostics'] == wind_diagnostics for line in lines)
    if len(complete) == 2:
        assert wind_diagnostics['requested'] and wind_diagnostics['rightReuses'] > 0
        assert wind_diagnostics['lastPairReuses'] == 1 and wind_diagnostics['lastPairMaxOriginalDelta'] == 3
    else:
        assert wind_diagnostics is None
    diagnostics = manifest['softCutoutDiagnostics']
    assert all(line['softCutoutDiagnostics'] == diagnostics for line in lines)
    if len(complete) == 2:
        assert diagnostics['schema'] == 1 and diagnostics['getterSkips'] > 0
        assert diagnostics['contractSkips'] > 0 and diagnostics['contractMismatchMask'] > 0
    else:
        assert diagnostics is None
    assert manifest['softCutoutTreeDraws'] == (16 if len(complete) == 2 else 0)
    assert manifest['softCutoutNamedDraws'] == (48 if len(complete) == 2 else 0) and manifest['softCutoutFailures'] == 0
    assert all(all(line[key] == manifest[key] for key in ('softCutoutsRequested','softCutoutTreeDraws','softCutoutNamedDraws','softCutoutFailures')) for line in lines)
    assert manifest['roadMipsRequested'] == (len(complete) == 1) and manifest['cutoutMipsRequested'] == (len(complete) == 2)
    assert manifest['cutoutMipUploads'] == (9 if len(complete) == 2 else 0) and manifest['cutoutMipFailures'] == 0
    assert all(all(line[key] == manifest[key] for key in ('roadMipsRequested','cutoutMipsRequested','cutoutMipUploads','cutoutMipFailures')) for line in lines)
    assert manifest['textureFilterChoice'] == len(complete) and manifest['renderScaleChoice'] == [1000,1250,1500][len(complete)]
    assert all(line['textureFilterChoice'] == manifest['textureFilterChoice'] and line['renderScaleChoice'] == manifest['renderScaleChoice'] for line in lines)
    mip_mode, mip_regions = (1, 12) if len(complete) == 1 else (0, 0)
    assert manifest['terrainMipFilterMode'] == mip_mode and manifest['terrainMipFilterRegions'] == mip_regions
    assert manifest['terrainMipFilterFailures'] == 0
    assert all(line['terrainMipFilterMode'] == mip_mode and line['terrainMipFilterRegions'] == mip_regions and line['terrainMipFilterFailures'] == 0 for line in lines)
    terrain_enabled = len(complete) == 2
    assert manifest['terrainDetailTestRequested'] == terrain_enabled and manifest['terrainDetailUploads'] == (3 if terrain_enabled else 0)
    assert all(line['terrainDetailTestRequested'] == terrain_enabled and line['terrainDetailUploads'] == (3 if terrain_enabled else 0) for line in lines)
    assert manifest['schema'] == 2 and (session / 'renderer-config.json').is_file()
    for index, line in enumerate(lines):
        states = line['drawStates']['eyes']
        assert [eye['states'][0]['textures'][0]['id'] for eye in states] == [10, 11]
        assert [eye['states'][0]['render'][1] for eye in states] == [index, index]
        assert line['drawStates']['vulkanEffectiveSampler'] == 'NOT_VERIFIED'
    phases = ['raw', 'final'] if manifest['aaApplied'] else ['raw']
    expected = {'raw': [bytes([0, 0, 255, 255]), bytes([255, 0, 0, 255])],
                'final': [bytes([0, 255, 0, 255]), bytes([255, 255, 0, 255])]}
    if len(complete) == 2:  # Actual SMAA preserves the flat red/blue fields.
        expected['final'] = [bytes([255, 0, 0, 255]), bytes([0, 0, 255, 255])]
    for phase in phases:
        for eye in range(2):
            for frame in range(8):
                assert (session / f'frame-{frame:02d}-eye-{eye}-{phase}.bin').read_bytes() == expected[phase][eye] * (17 * 13)
    assert len(list(session.glob('*.bin'))) == 8 * 2 * len(phases)
    complete.append(manifest['aaApplied'])
assert complete == [False, True, True]
assert json.loads((sessions / 'quest-eye-capture.status').read_text())['state'] == 'FAILED_SETUP'
assert result.stderr.count('INCOMPLETE: 0/8') == 2
report = {'status': 'HOST_GPU_READBACK_PASS', 'framesVerified': 24, 'binaryImagesVerified': 80,
          'checks': 'dormant without request; actual BGRA/RGBA bytes; actual SMAA/readback barrier chain; separate eyes/raw/final; exact timestamps/camera/draw-state association; eight-frame bound; restart; abort/write failure never complete; unsupported source rejected; draw deduplication, scope, hard limits, failed-getter masks and escaped names; config privacy whitelist; neutral-terrain RGB exhaustive alpha/off/pitch/guard/rejection checks and capture metadata off/on; terrain mip checkerboard/ramp, isolated material gutters, exact A1/A8 alpha and bounded off/rejection',
          'softCutoutFixture': 'PASS: off/getter failures inert; captured nine-state prefixes plus RGB/RGBA host contracts, two-pass blend/depth, exact restore, all setup/draw/restore failure paths and nested eye/tree scope; software alpha model, not native GPU cutout quality',
          'cutoutAlphaFixture': 'PASS: names, regenerated A8 mips/coverage, exact base/RGB/padding, endpoints, off/invalid/overlap and infeasible coverage',
          'graphicsChoiceFixture': 'PASS: POINT/NONE identity and bounded eye extents',
          'treeWindFixture': 'PASS: Off identity, exact same-pair thirty-component reuse, unchanged independent state steps, owner/atlas/breeze identity, missing/stale/ambiguous/outside/nested/capacity/nonfinite fallbacks and JSON round-trip; not game replay or native GPU wind evidence',
          'treeContourFixture': 'PASS: exact impulse alpha/premultiplied RGB, opaque internal tile continuity, isolated adjacent materials, uniform fractional/opaque and 1x1 identity, unaligned guards/padding/gaps, Off bytes/counters, invalid-late/overlap/count/dimension/pitch/address rejection; upload JSON round-trip. Not native GPU tree quality.',
          'terrainFarMipFixture': 'PASS: 406 ordinary packed footprints within LOD3 gutters, layout/ownership/cliff-bound/nonfinite rejection, exact pitched A1R5G5B5 checkerboard box averages, preserved source/padding/guards, alias/null/odd/stride rejection, adjacent material isolation, request-versus-upload JSON round-trip. Gentle LOD3 periodic impulse/channel/endpoint/large-material/padding/alpha/transaction fixtures pass. Native resource cloning and Quest activation/quality remain NOT_VERIFIED.',
          'fenceContourFixture': 'PASS: periodic premultiplied row impulse at five levels, no vertical packed-pattern mixing, Off/source/padding exact, invalid-late/overlap/address/pitch/layout rejection; per-eye diagnostics round-trip. Not native GPU fence quality.',
          'questRuntime': 'NOT_VERIFIED', 'fixtureDirectory': str(sessions)}
(root / 'work/evidence/eye-capture-host.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report))
