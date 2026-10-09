import pathlib,sys,tempfile,re
roots=[pathlib.Path(__file__).resolve().parents[3],pathlib.Path(tempfile.gettempdir()).resolve()]
parents=False
for value in sys.argv[1:]:
 if value in ('-p','--parents'): parents=True; continue
 if value == '--': continue
 if value.startswith('-'): raise SystemExit('unsupported mkdir option: '+value)
 if re.match(r'^/[a-zA-Z]/',value): value=value[1]+':'+value[2:]
 target=pathlib.Path(value).resolve()
 if not any(target.is_relative_to(root) for root in roots): raise SystemExit('mkdir target outside build/temp roots: '+str(target))
 if target.is_dir() and parents: continue
 target.mkdir(parents=parents,exist_ok=parents)
