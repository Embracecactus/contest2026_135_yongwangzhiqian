from pathlib import Path
import tempfile,subprocess,resource,json
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
r=Path.cwd();out=r/'out/shaniu-s114';driver=(r/'tests/host/bk7258/test_pack_trial.py').read_text()
needle='  if (release_error)\n    g_bkdisplay_selection.state = BKDISPLAY_SELECTION_UNKNOWN;'
source=(r/'app/bk7258/bk7258_display_selection.inc').read_text();assert source.count(needle)==1
with tempfile.TemporaryDirectory(prefix='catalog-release-mutant-') as d:
 p=Path(d)
 driver='import sys\nsys.path.insert(0, '+repr(str(r/'tests/host/bk7258'))+')\n'+driver
 driver=driver.replace('ROOT = Path(__file__).resolve().parents[3]','ROOT = Path('+repr(str(r))+')').replace('HERE = ROOT / "tests/host/bk7258"','HERE = ROOT / "tests/host/bk7258"')
 insertion='''        (temp / "bk7258_display_selection.inc").write_text(
            (APP / "bk7258_display_selection.inc").read_text().replace(
                %r, %r))
''' % (needle,'  if (release_error && !request.catalog)\n    g_bkdisplay_selection.state = BKDISPLAY_SELECTION_UNKNOWN;')
 driver=driver.replace('        temp = Path(d)\n','        temp = Path(d)\n'+insertion)
 (p/'driver.py').write_text(driver)
 with (out/'mutation-detected.log').open('w') as f:
  result=subprocess.run(['python3',p/'driver.py','catalog-job-release'],stdout=f,stderr=subprocess.STDOUT)
 log=(out/'mutation-detected.log').read_text()
 assert result.returncode!=0 and 'status.state == BKDISPLAY_SELECTION_UNKNOWN' in log and 'SETUP_ERROR' not in log
 with (out/'mutation-restored.log').open('w') as f:
  restore=subprocess.run(['python3',r/'tests/host/bk7258/test_pack_trial.py','catalog-job-release'],stdout=f,stderr=subprocess.STDOUT)
 assert restore.returncode==0
 (out/'mutation.json').write_text(json.dumps(dict(mutation='allow catalog DONE despite release failure',compiled=True,assertion='status.state == BKDISPLAY_SELECTION_UNKNOWN',mutant_exit=result.returncode,restore_exit=restore.returncode,production_modified=False),indent=2)+'\n')
 print('Catalog release mutation detected; restore PASS')
