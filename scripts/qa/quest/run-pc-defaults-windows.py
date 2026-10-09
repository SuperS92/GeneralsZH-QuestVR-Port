#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual launcher marker helper with private temporary folders."""
import argparse,json,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools',required=True,type=pathlib.Path)
args=parser.parse_args()
tools=json.loads(args.tools.read_text())
jdk=pathlib.Path(tools['jdk'])/'bin'
work=root/'work/pc-defaults-test';work.mkdir(exist_ok=True)
subprocess.run([str(jdk/'javac.exe'),'-d',str(work),
    str(root/'android/app/src/main/java/com/generalsx/zerohour/QuestPcCompatibility.java'),
    str(root/'scripts/qa/quest/QuestPcCompatibilityTest.java')],check=True)
result=subprocess.run([str(jdk/'java.exe'),'-cp',str(work),
    'com.generalsx.zerohour.QuestPcCompatibilityTest',str(work)],capture_output=True,text=True,timeout=30)
print(result.stdout+result.stderr)
if result.returncode: raise SystemExit(result.returncode)
(root/'work/evidence/pc-defaults-host.json').write_text(json.dumps(dict(status='HOST_PC_DEFAULT_MARKER_PASS',
    productionHelper=True,limits='Android preference UI, imported-folder validation and actual network match not exercised here.'),indent=2))
