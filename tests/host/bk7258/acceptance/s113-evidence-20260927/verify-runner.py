from pathlib import Path
import hashlib,json,platform,subprocess,time
root=Path.cwd();out=root/'out/shaniu-s113';here=root/'tests/host/bk7258';rows=[]
cases=('missing','pages','cancel-before','cancel-during','invalid-cursor','read-error','directory-close','file-close','scan-limit','symlink','corrupt')
commands=[('RES-02.catalog-'+v,[here/'build/test_display_catalog',here/'build/shaniu-default-v1.bkep',v]) for v in cases]
commands += [('RES-01.upload-'+v,[here/'build/test_display_upload',here/'build/shaniu-default-v1.bkep',v,here/'build/upload-second.bkep']) for v in ('collision','cancel','corrupt','fragmented','normal','preserve','write-failure','sync-failure','close-failure','directory-failure')]
commands += [('RES-02.'+v,[here/'build/test_display_upload',here/'build/shaniu-default-v1.bkep',v,here/'build/upload-second.bkep']) for v in ('activate-collision','activate-directory-failure')]
commands += [('RES-02.web-display-tls-'+v,['python3',here/'test_pack_trial.py','web-display-tls-'+v]) for v in ('trial_expiry','trial_cancel','missing_pack','default_supersedes_trial','release_recovery_stays_unknown')]
inputs={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'app/bk7258/bk7258_display_pack.c',root/'app/bk7258/bk7258_display_store.c',here/'test_display_catalog.c',here/'build/shaniu-default-v1.bkep',here/'build/upload-second.bkep']}
for id,cmd in commands:
 start=time.monotonic()
 with (out/(id+'.log')).open('w') as f:
  f.write(json.dumps([str(a) for a in cmd])+'\n');f.flush()
  p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 rows.append(dict(id=id,status='PASS' if p.returncode==0 else 'FAIL_ASSERTION',returncode=p.returncode,seconds=time.monotonic()-start))
 print(id,p.returncode,flush=True)
assert all(hashlib.sha256((root/p).read_bytes()).hexdigest()==h for p,h in inputs.items())
registry=json.loads((here/'acceptance/required-units.v1.json').read_text())
assert len(set(r['id'] for r in rows))==len(rows)
assert set(r['id'] for r in rows)<=set(registry['ids'])
report=dict(scope='S113 affected catalog/upload and HTTP TLS display paths; not full global suite',parent=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),selected_execution_ids=[r['id'] for r in rows],collected=rows,inputs=inputs,registered_global_ids=len(registry['ids']),last_full_suite='s111-20260927.json',environment=dict(platform=platform.platform(),python=platform.python_version()))
(out/'selected.json').write_text(json.dumps(report,indent=2)+'\n')
raise SystemExit(any(r['returncode'] for r in rows))
