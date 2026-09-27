from pathlib import Path
import hashlib,json,platform,subprocess,sys,time,unittest
root=Path.cwd();sys.path.insert(0,str(root/'tests/host/bk7258'))
import test_workbench_web as t
base=root/'tests/host/bk7258/acceptance';out=root/'out/shaniu-s112'
manifest=json.loads((base/'required-units.v1.json').read_text())
required=sorted(i for i in manifest['ids'] if i.startswith('USB-02.browser-'))
inputs={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ('tools/bk7258/_lib/workbench_web.py','tools/bk7258/_lib/workbench.py','tools/bk7258/_lib/workbench_resources.py','tests/host/bk7258/test_workbench_web.py','tests/host/bk7258/test_workbench_client.py','tests/host/bk7258/test_workbench_resource_flow.py','tests/host/bk7258/build/test_display_job_control','tests/host/bk7258/build/shaniu-default-v1.bkep')}
records=[]
class Result(unittest.TextTestResult):
 def startTest(self,test):
  self.started=time.monotonic();self.state='NOT_RUN';super().startTest(test)
 def addSuccess(self,test):self.state='PASS';super().addSuccess(test)
 def addFailure(self,test,error):self.state='FAIL_ASSERTION';super().addFailure(test,error)
 def addError(self,test,error):self.state='SETUP_ERROR';super().addError(test,error)
 def addSkip(self,test,why):self.state='SKIP';super().addSkip(test,why)
 def stopTest(self,test):
  records.append(dict(id='USB-02.browser-'+test._testMethodName.removeprefix('test_'),parent='USB-02',layer='L2',status=self.state,seconds=time.monotonic()-self.started))
  super().stopTest(test)
with (out/'selected.log').open('w') as stream:
 result=unittest.TextTestRunner(stream=stream,verbosity=2,resultclass=Result).run(unittest.defaultTestLoader.loadTestsFromTestCase(t.WebTest))
assert sorted(r['id'] for r in records)==required
assert all(hashlib.sha256((root/p).read_bytes()).hexdigest()==h for p,h in inputs.items())
report=dict(report_scope='S112 complete affected HTTP workbench suite, not global collection',checkout_head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),last_full_suite='s111-20260927.json',registered_global_ids=len(manifest['ids']),selected_execution_ids=required,collected=records,counts={s:sum(r['status']==s for r in records) for s in ('PASS','FAIL_ASSERTION','SETUP_ERROR','SKIP','NOT_RUN')},inputs=inputs,inputs_unchanged_during_run=True,environment=dict(platform=platform.platform(),python=platform.python_version()),collection_errors=[])
(out/'selected.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(report['counts'], 'global registry:',len(manifest['ids']), 'not all executed this slice')
sys.exit(0 if result.wasSuccessful() and all(r['status']=='PASS' for r in records) else 1)
