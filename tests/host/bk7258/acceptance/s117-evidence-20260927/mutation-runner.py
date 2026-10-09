from pathlib import Path
import tempfile,subprocess,json
r=Path.cwd();out=r/'out/shaniu-s117';source=(r/'tools/bk7258/_lib/workbench_catalog.py').read_text()
needle='or any(data[96 + count * 128 :])';assert source.count(needle)==1
with tempfile.TemporaryDirectory(prefix='catalog-padding-mutant-') as d:
 p=Path(d)/'check.py'
 p.write_text('import sys,types,unittest\n'+'sys.path[:0]='+repr([str(r/'tools/bk7258'),str(r/'tests/host/bk7258')])+'\n'+"import _lib\nm=types.ModuleType('_lib.workbench_catalog');m.__package__='_lib'\n"+'exec(compile('+repr(source.replace(needle,''))+',"isolated-catalog.py","exec"),m.__dict__)\n'+"sys.modules['_lib.workbench_catalog']=m\nimport test_workbench_catalog as t\nresult=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromName('CatalogTest.test_malformed',t))\nraise SystemExit(0 if result.wasSuccessful() else 1)\n")
 with (out/'mutation.log').open('w') as f:result=subprocess.run(['python3',p],stdout=f,stderr=subprocess.STDOUT)
 log=(out/'mutation.log').read_text();assert result.returncode==1 and 'offset=224' in log and 'ValueError not raised' in log,log
 with (out/'restored.log').open('w') as f:restore=subprocess.run(['python3',r/'tests/host/bk7258/test_workbench_catalog.py','CatalogTest.test_malformed'],stdout=f,stderr=subprocess.STDOUT)
 assert restore.returncode==0
 (out/'mutation.json').write_text(json.dumps(dict(mutation='omit zero unused catalog slot check',compiled=True,detected=True,mutant_exit=result.returncode,restored_exit=restore.returncode,production_modified=False),indent=2)+'\n')
print('Unused catalog slot mutation detected; restore PASS')
