#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify a privately retrieved session and decode pixels without filtering."""
import argparse
import hashlib
import json
import math
import pathlib
import struct
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('session', type=pathlib.Path)
args = parser.parse_args()
session = args.session.resolve()
manifest = json.loads((session / 'manifest.json').read_text())
frames = [json.loads(line) for line in (session / 'frames.jsonl').read_text().splitlines()]
if manifest['schema'] not in (1, 2) or len(frames) != 8 or [f['index'] for f in frames] != list(range(8)):
    raise SystemExit('NOT_VERIFIED: incomplete/unsupported frame sequence')
draw_summary = {'status': 'NOT_RECORDED_SCHEMA_1'}
if manifest['schema'] == 2:
    config = json.loads((session / 'renderer-config.json').read_text())
    if config.get('schema') != 1 or config.get('inputsOnlyNoRuntimeConfirmation') is not True:
        raise SystemExit('NOT_VERIFIED: unsupported renderer config provenance')
    coverage, observed = [], {}
    for frame in frames:
        draw = frame.get('drawStates')
        if not isinstance(draw, dict) or draw.get('schema') != 1 or len(draw.get('eyes', [])) != 2:
            raise SystemExit('NOT_VERIFIED: missing per-frame draw metadata')
        for eye_index, eye in enumerate(draw['eyes']):
            states = eye.get('states', [])
            if len(states) > 256 or eye['omitted'] < 0 or eye['draws'] < 0 or \
                    sum(s['count'] for s in states) + eye['omitted'] != eye['draws'] or \
                    eye['truncated'] != (eye['omitted'] > 0):
                raise SystemExit('NOT_VERIFIED: inconsistent bounded draw counts')
            failed = 0
            for state in states:
                if state['count'] < 1 or len(state['textures']) != 8 or len(state['render']) != 9:
                    raise SystemExit('NOT_VERIFIED: invalid draw-state shape')
                if state['renderValidMask'] != 511: failed += 1
                for stage, texture in enumerate(state['textures']):
                    if len(texture['sampler']) != 9 or len(texture['widths']) != 16 or len(texture['heights']) != 16:
                        raise SystemExit('NOT_VERIFIED: invalid texture-state shape')
                    if not texture['bindingValid'] or texture['samplerValidMask'] != 511: failed += 1
                    if not texture['id']: continue
                    bits = texture['sampler'][3]
                    bias = struct.unpack('<f', struct.pack('<I', bits))[0]
                    value = {'stage': stage, 'id': texture['id'], 'nameHint': texture['nameHint'],
                             'type': texture['type'], 'levels': texture['levels'],
                             'levelValidMask': texture['levelValidMask'], 'widths': texture['widths'],
                             'heights': texture['heights'], 'managedLod': texture['managedLod'],
                             'format': texture['format'], 'sampler': texture['sampler'],
                             'samplerValidMask': texture['samplerValidMask'],
                             'd3dLodBias': bias if math.isfinite(bias) else 'NONFINITE',
                             'render': state['render'], 'renderValidMask': state['renderValidMask']}
                    key = json.dumps(value, sort_keys=True)
                    if key not in observed: observed[key] = dict(value, occurrences=[])
                    observed[key]['occurrences'].append({'frame': frame['index'], 'eye': eye_index, 'drawCount': state['count']})
            coverage.append({'frame': frame['index'], 'eye': eye_index, 'entered': eye['entered'],
                             'draws': eye['draws'], 'distinctStates': len(states), 'omitted': eye['omitted'],
                             'truncated': eye['truncated'], 'failedGetterGroups': failed})
    draw_summary = {'status': 'DRAW_METADATA_PRESENT', 'coverage': coverage,
                    'configInputs': config, 'observedTextureStates': list(observed.values()),
                    'vulkanEffectiveSampler': 'NOT_VERIFIED', 'sampledPixelLod': 'NOT_VERIFIED',
                    'note': 'Bound stages may be disabled; omitted/direct-device draws are outside coverage. Config inputs do not prove runtime consumption.'}
    (session / 'draw-state-summary.json').write_text(json.dumps(draw_summary, indent=2), encoding='utf-8')
width, height = manifest['width'], manifest['height']
if not (0 < width <= 4096 and 0 < height <= 4096):
    raise SystemExit('Invalid extent')
output = session / 'decoded'
output.mkdir(exist_ok=True)
hashes = {}
for frame in frames:
    for eye in range(2):
        for phase in ('raw', 'final') if manifest['aaApplied'] else ('raw',):
            path = session / f"frame-{frame['index']:02d}-eye-{eye}-{phase}.bin"
            pixels = path.read_bytes()
            if len(pixels) != width * height * 4:
                raise SystemExit('NOT_VERIFIED: unexpected image byte count: ' + path.name)
            format_id = manifest['sourceFormats'][eye] if phase == 'raw' else manifest['finalFormat']
            if format_id not in (37, 44):
                raise SystemExit('Unsupported pixel format')
            image = Image.frombytes('RGBA', (width, height), pixels, 'raw', 'RGBA' if format_id == 37 else 'BGRA')
            image.save(output / (path.stem + '.png'))
            hashes[path.name] = hashlib.sha256(pixels).hexdigest()
            del image, pixels
    if len(frame['eyes']) != 2 or not all(e['cameraValid'] for e in frame['eyes']):
        raise SystemExit('NOT_VERIFIED: missing effective camera')
    for e in frame['eyes']:
        values = e['positionMeters'] + e['orientationXYZW'] + e['fovLeftRightUpDown'] + e['cameraToWorld3x4']
        if not all(math.isfinite(v) for v in values):
            raise SystemExit('NOT_VERIFIED: invalid pose/matrix')
deltas = []
for eye in range(2):
    first = frames[0]['eyes'][eye]
    maximum = 0.0
    world_maximum = 0.0
    for frame in frames:
        current = frame['eyes'][eye]
        maximum = max(maximum, math.dist(first['positionMeters'], current['positionMeters']))
        world_maximum = max(world_maximum, math.dist(first['cameraToWorld3x4'], current['cameraToWorld3x4']))
    deltas.append({'eye': eye, 'maxPositionDeltaMetersFromFirst': maximum,
                   'maxMatrixElementL2DeltaFromFirst': world_maximum})
report = {'status': 'COMPLETE_LOSSLESS_CAPTURE', 'frameCount': len(frames),
          'imageCount': len(hashes), 'aaRequested': manifest['aaRequested'], 'aaApplied': manifest['aaApplied'],
          'elapsedMilliseconds': (frames[-1]['monotonicNs'] - frames[0]['monotonicNs']) / 1e6,
          'renderFrames': [f['renderFrame'] for f in frames], 'cameraDeltas': deltas, 'sha256': hashes,
          'drawMetadata': {k: v for k, v in draw_summary.items() if k != 'observedTextureStates'},
          'rootCause': 'NOT_VERIFIED', 'note': 'Pose/camera changes and animations confound unaligned pixel differences; diagnostic capture is not a performance benchmark.'}
(session / 'analysis.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({k: v for k, v in report.items() if k != 'sha256'}, indent=2))
