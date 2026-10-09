from pathlib import Path
import tempfile,subprocess,json,resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
r=Path.cwd();out=r/'out/shaniu-s115';driver=(r/'tests/host/bk7258/test_pack_trial.py').read_text()
needle='''  allowed = allowed && g_bkdisplay_power_requested == 0 &&
            !g_bkdisplay_power_pending;'''
source=(r/'app/bk7258/bk7258_display_intent.inc').read_text();assert source.count(needle)==1
with tempfile.TemporaryDirectory(prefix='power-gate-mutant-') as d:
 p=Path(d)
 driver='import sys\nsys.path.insert(0, '+repr(str(r/'tests/host/bk7258'))+')\n'+driver
 driver=driver.replace('ROOT = Path(__file__).resolve().parents[3]','ROOT = Path('+repr(str(r))+')')
 insert='''        (temp / "bk7258_display_intent.inc").write_text(
            (APP / "bk7258_display_intent.inc").read_text().replace(%r, %r))
''' % (needle,'  /* isolated stale readiness reopens power gate */')
 driver=driver.replace('        temp = Path(d)\n','        temp = Path(d)\n'+insert)
 (p/'driver.py').write_text(driver)
 with (out/'mutation.log').open('w') as f:
  result=subprocess.run(['python3',p/'driver.py','power-request-held-lock'],stdout=f,stderr=subprocess.STDOUT)
 log=(out/'mutation.log').read_text()
 assert result.returncode!=0 and 'bk7258_display_request_expression("happy", &selection_id) == -EBUSY' in log and 'SETUP_ERROR' not in log
 with (out/'restored.log').open('w') as f:
  restore=subprocess.run(['python3',r/'tests/host/bk7258/test_pack_trial.py','power-request-held-lock'],stdout=f,stderr=subprocess.STDOUT)
 assert restore.returncode==0
 (out/'mutation.json').write_text(json.dumps(dict(mutation='stale worker readiness reopens power admission',compiled=True,detected=True,mutant_exit=result.returncode,restored_exit=restore.returncode,production_modified=False),indent=2)+'\n')
print('Power gate mutation detected; restore PASS')
