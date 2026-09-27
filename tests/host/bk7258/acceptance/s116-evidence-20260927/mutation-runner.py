from pathlib import Path
import tempfile,subprocess,json,resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
r=Path.cwd();out=r/'out/shaniu-s116';driver=(r/'tests/host/bk7258/test_pack_trial.py').read_text()
source=(r/'app/bk7258/bk7258_display_selection_control.c').read_text()
needle='(job.catalog ? 32 : 0)';assert source.count(needle)==1
with tempfile.TemporaryDirectory(prefix='catalog-type-mutant-') as d:
 p=Path(d); mutant=p/'selection-control.c';mutant.write_text(source.replace(needle,'0 /* isolated missing catalog type */'))
 driver='import sys\nsys.path.insert(0, '+repr(str(r/'tests/host/bk7258'))+')\n'+driver
 driver=driver.replace('ROOT = Path(__file__).resolve().parents[3]','ROOT = Path('+repr(str(r))+')')
 driver=driver.replace('str(APP / "bk7258_display_selection_control.c")',repr(str(mutant)))
 (p/'driver.py').write_text(driver)
 with (out/'mutation.log').open('w') as f:result=subprocess.run(['python3',p/'driver.py','catalog-wire-normal'],stdout=f,stderr=subprocess.STDOUT)
 log=(out/'mutation.log').read_text()
 assert result.returncode!=0 and 'get32(response+28)==32' in log and 'SETUP_ERROR' not in log,log[-3000:]
 with (out/'restored.log').open('w') as f:restore=subprocess.run(['python3',r/'tests/host/bk7258/test_pack_trial.py','catalog-wire-normal'],stdout=f,stderr=subprocess.STDOUT)
 assert restore.returncode==0
 (out/'mutation.json').write_text(json.dumps(dict(mutation='omit shared latest-job catalog type flag',compiled=True,detected=True,mutant_exit=result.returncode,restored_exit=restore.returncode,production_modified=False),indent=2)+'\n')
print('Catalog type mutation detected; restore PASS')
