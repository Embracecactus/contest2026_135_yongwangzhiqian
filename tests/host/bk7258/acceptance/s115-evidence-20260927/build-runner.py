from pathlib import Path
import os,json,re,subprocess,sys
root=Path('/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian');sys.path.insert(0,str(root/'tools/bk7258'));from _lib import sdk
report=sdk.verify(root,'ap-aidk')
role=root.parent/'out/bk7258/aidk_ai_toy/app__openvela_ap/bk7258-510173147382a879/roles/mcuboot/ap/bk7258-role-5a3061a4da19ae9f';tree=role/'cmake';generated=role/'generated'
header=(generated/'bk7258_partitions.h').read_text();identity=re.search(r'#define BK7258_LAYOUT_ID "([^"]+)"',header)[1];digest=re.search(r'#define BK7258_LAYOUT_SHA256 "([^"]+)"',header)[1]
public=next(row['file'] for row in json.loads((tree/'compile_commands.json').read_text()) if 'catalog_public' in row['file']);assert Path(public).is_file()
toolchain=root/'prebuilt/gcc-arm-none-eabi-10.3-2021.10/bin'
env=dict(os.environ,BK7258_SDK_DIR=str(report.bundle),BK7258_TOOLCHAIN_BIN=str(toolchain),BK7258_PARTITION_HEADER=str(generated/'bk7258_partitions.h'),BK7258_PARTITION_LINKER=str(generated/'bk7258_partitions.ld'),BK7258_PARTITION_SDK_CSV=str(generated/'sdk_partitions.csv'),BK7258_PARTITION_ID=identity,BK7258_PARTITION_SHA256=digest,BK7258_OTA_CATALOG_PUBLIC_SOURCE=public)
env['PATH']=str(toolchain)+os.pathsep+env['PATH']
with (root/'out/shaniu-s115/ap.log').open('w') as log:r=subprocess.run([str(root.parent/'prebuilts/tools/cmake/bin/cmake'),'--build',str(tree),'-j4'],env=env,stdout=log,stderr=subprocess.STDOUT)
print('AP incremental',r.returncode);raise SystemExit(r.returncode)
