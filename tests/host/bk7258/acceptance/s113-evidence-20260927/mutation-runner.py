from pathlib import Path
import subprocess,tempfile,json,resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
r=Path.cwd();app=r/'app/bk7258';out=r/'out/shaniu-s113';s=(app/'bk7258_display_store.c').read_text()
needle='''  if (closedir(dir) < 0)
    {
      ret = bkdisplay_store_errno();
    }'''
assert s.count(needle)==1
with tempfile.TemporaryDirectory(prefix='shaniu-catalog-mutation-') as d:
 p=Path(d);(p/'store.c').write_text(s.replace(needle,'  closedir(dir); /* isolated ignored close error */'))
 cmd=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-O2',str(r/'tests/host/bk7258/test_display_catalog.c'),str(app/'bk7258_display_pack.c'),str(p/'store.c'),'-I',str(app),'-Wl,--wrap=opendir,--wrap=readdir,--wrap=closedir,--wrap=close,--wrap=write','-o',str(p/'test')]
 with (out/'mutation-build.log').open('w') as f: subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
 with (out/'mutation-detected.log').open('w') as f:
  result=subprocess.run([p/'test',r/'tests/host/bk7258/build/shaniu-default-v1.bkep','directory-close'],stdout=f,stderr=subprocess.STDOUT)
 assert result.returncode!=0 and 'ret == -EIO' in (out/'mutation-detected.log').read_text()
 with (out/'mutation-restored.log').open('w') as f:
  restored=subprocess.run([r/'tests/host/bk7258/build/test_display_catalog',r/'tests/host/bk7258/build/shaniu-default-v1.bkep','directory-close'],stdout=f,stderr=subprocess.STDOUT)
 assert restored.returncode==0
 (out/'mutation.json').write_text(json.dumps(dict(mutation='ignore directory close error',compiled=True,detected_assertion='ret == -EIO',mutant_exit=result.returncode,restored_exit=restored.returncode,production_edited=False),indent=2)+'\n')
print('Mutation detected; unchanged production restore PASS')
