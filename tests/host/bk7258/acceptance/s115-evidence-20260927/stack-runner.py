from pathlib import Path
import json,shlex,subprocess,os
r=Path.cwd();out=r/'out/shaniu-s115';role=r.parent/'out/bk7258/aidk_ai_toy/app__openvela_ap/bk7258-510173147382a879/roles/mcuboot/ap/bk7258-role-5a3061a4da19ae9f/cmake'
commands=json.loads((role/'compile_commands.json').read_text())
for name in ('bk7258_display_service.c',):
 row=next(x for x in commands if x['file'].endswith('/'+name));args=shlex.split(row['command']);obj=out/(name+'.o')
 args[args.index('-o')+1]=str(obj);args+=['-fstack-usage']
 with (out/(name+'.compile.log')).open('w') as f:subprocess.run(args,cwd=row['directory'],stdout=f,stderr=subprocess.STDOUT,check=True)
print('Compiled exact target flags into isolated output with -fstack-usage')
