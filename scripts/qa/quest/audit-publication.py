#!/usr/bin/env python3
"""Review added reachable Git blobs and APK entries without printing secret values.

A pattern review supplements manual inspection; it does not prove that no secret
exists. An ignored deny-file can supply private device/name strings locally.
"""
import argparse, json, pathlib, re, subprocess, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
PATTERNS = {
    'private_key': rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----',
    'credential': rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|sk-(?:proj-)?[A-Za-z0-9_-]{40,}|AKIA[0-9A-Z]{16}|AIza[A-Za-z0-9_-]{35})',
    'user_path': rb'(?i:[A-Z]:[/\\]+Users[/\\]+(?!Public\b|Default\b)[^/\\\s"\r\n]+|[/]home[/][^/\s"\r\n]+|[/][a-z][/]Users[/][^/\s"\r\n]+)',
    'literal_secret': rb'(?i:(?:password|api[_-]?key|refresh[_-]?token|client[_-]?secret)\s*[:=]\s*["\'][^"\'\r\n]{16,}["\'])',
}


def private_marker(value, encoding):
    """Avoid confusing a short name with part of a public font glyph name."""
    literal = re.escape(value.encode(encoding))
    if value.isalpha() and len(value) <= 12:
        letter = rb'[A-Za-z]' if encoding == 'utf-8' else rb'[A-Za-z]\x00'
        literal = rb'(?<!' + letter + rb')' + literal + rb'(?!' + letter + rb')'
    return re.compile(literal, re.I)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--git', default='git')
    p.add_argument('--ref', default='HEAD')
    p.add_argument('--apk', type=pathlib.Path)
    p.add_argument('--deny-file', type=pathlib.Path)
    p.add_argument('--output', type=pathlib.Path, required=True)
    a = p.parse_args()
    patterns = {k: re.compile(v) for k, v in PATTERNS.items()}
    denied = json.loads(a.deny_file.read_text(encoding='utf-8')) if a.deny_file else []
    for i, value in enumerate(denied):
        for encoding in ('utf-8', 'utf-16le'):
            patterns[f'private_marker_{i}_{encoding}'] = private_marker(value, encoding)
    def git(*args):
        return subprocess.check_output([a.git, '-c', f'safe.directory={ROOT.as_posix()}', *args], cwd=ROOT)
    def batch(specs):
        if not specs: return {}
        data = subprocess.check_output([a.git, '-c', f'safe.directory={ROOT.as_posix()}',
                                       'cat-file', '--batch'], cwd=ROOT,
                                      input=('\n'.join(specs)+'\n').encode())
        result, offset = {}, 0
        for spec in specs:
            end = data.index(b'\n',offset)
            header = data[offset:end].split(); offset = end+1
            if header[-1] == b'missing': result[spec] = (b'missing', b''); continue
            size = int(header[-1]); result[spec] = (header[-2],data[offset:offset+size]); offset += size+1
        return result
    base = json.loads((ROOT/'docs/WORKDIR/quest/UPSTREAM_PIN.json').read_text(encoding='utf-8'))['commit']
    git('merge-base', '--is-ancestor', base, a.ref)
    findings, inherited = [], []
    delta = git('diff', '--name-only', base, a.ref).decode().splitlines()
    unsafe_names = [name for name in delta if re.search(r'(?i)(?:^|/)\.env(?:\.|$)|\.(?:rep|sav|log|big|p12|pfx|jks|keystore|apk)$',name)]
    unsafe_entries = batch([f'{a.ref}:{name}' for name in unsafe_names])
    unsafe = [name for name in unsafe_names if unsafe_entries[f'{a.ref}:{name}'][0] != b'missing']
    scanned = 0
    objects = [line.partition(' ')[::2] for line in git('rev-list','--objects',a.ref,'--not',base).decode().splitlines()]
    contents = batch([oid for oid, name in objects])
    prior = batch([f'{base}:{name}' for oid, name in objects if contents[oid][0] == b'blob'])
    for oid, name in objects:
        kind, data = contents[oid]
        if kind != b'blob': continue
        scanned += 1
        previous = prior[f'{base}:{name}'][1].splitlines()
        for kind, pattern in patterns.items():
            for match in pattern.finditer(data):
                start = data.rfind(b'\n',0,match.start())+1
                end = data.find(b'\n',match.end())
                row = (data[start:] if end < 0 else data[start:end]).rstrip(b'\r')
                (inherited if row in previous else findings).append({'path':name,'object':oid,'category':kind})
    metadata = git('log',f'{base}..{a.ref}','--format=%an <%ae>%n%cn <%ce>%n%B')
    for kind, pattern in patterns.items():
        if pattern.search(metadata): findings.append({'path':'additional commit metadata','category':kind})
    apk_entries = 0
    if a.apk:
        with zipfile.ZipFile(a.apk) as z:
            if z.testzip(): raise SystemExit('APK integrity failed')
            for name in z.namelist():
                apk_entries += 1
                data = z.read(name)
                if re.search(r'(?i)\.(?:big|rep|sav|p12|pfx|jks|keystore)$', name):
                    findings.append({'path':name,'category':'private_or_game_file'})
                for kind, pattern in patterns.items():
                    if pattern.search(data): findings.append({'path':name,'category':kind})
    report = {'status':'PATTERN_PREFLIGHT_PASS' if not findings and not unsafe else 'REVIEW_REQUIRED',
              'ref':a.ref,'base':base,'additionalBlobsScanned':scanned,'apkEntriesScanned':apk_entries,
              'sensitiveFilenameCandidates':unsafe,'findings':findings,'inheritedUpstreamMatches':inherited,
              'limits':'Pattern scan; manual content, source-offer and exact upload-ref review are still required'}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
    if findings or unsafe: raise SystemExit(1)

if __name__ == '__main__': main()
