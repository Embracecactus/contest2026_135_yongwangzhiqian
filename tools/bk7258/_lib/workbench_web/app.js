'use strict';
const $ = id => document.getElementById(id);
const token = location.hash.slice(1) || sessionStorage.getItem('shaniu-local-token') || '';
if (location.hash) { sessionStorage.setItem('shaniu-local-token', token); history.replaceState(null, '', '/'); }
let busy = false, trial = null, selection = null, lastSeen = null, latest = null, catalog = null, catalogReceipt = null;
const labels = {running:'正在执行',returned:'设备已返回结果',unconfirmed:'结果未知',disconnected:'未连接',unauthorized:'未授权',unsupported:'不支持',failed:'失败'};
async function api(path, body) {
  const response = await fetch(path,{method:body?'POST':'GET',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || '本地工作台不可用');
  return data;
}
function gates() {
  document.querySelectorAll('button').forEach(b=>{b.disabled=busy;});
  $('cancel').disabled=!(busy && latest && ['resource-upload','resource-resume','resource-cancel'].includes(latest.operation));
  $('trial-start').disabled=busy || !trial || !['idle','expired','canceled','superseded','failed'].includes(trial.state);
  $('trial-cancel').disabled=busy || !trial || !['pending','active','cancel_pending'].includes(trial.state);
  $('default-refresh').disabled=busy || !selection;
  $('default-set').disabled=busy || !selection?.version_known;
  const terminal=catalog && ['idle','done','canceled','failed','unknown'].includes(catalog.state) && !catalog.release_error;
  $('catalog-refresh').disabled=busy || !terminal;
  $('catalog-next').disabled=busy || !catalog?.page_available || !catalog?.more;
  $('catalog-cancel').disabled=busy || !catalog?.catalog || !['pending','preparing','cancel_pending'].includes(catalog.state);
  $('catalog-recover').disabled=busy || !catalog?.catalog || catalog.state!=='unknown' || !catalog.release_error || catalog.recovery_pending;
  $('export').disabled=!latest;
}
function renderCatalog() {
  $('catalog-items').replaceChildren();
  if (!catalog) { $('catalog-state').textContent='目录结果未确认，请重新读取状态。'; return; }
  if (!catalog.page_available) { $('catalog-state').textContent='目录状态：'+catalog.state+'。尚无本次可用列表，请读取状态或刷新目录。'; return; }
  $('catalog-state').textContent=catalog.entries.length ? '本页 '+catalog.entries.length+' 项素材'+(catalog.more?'，还有下一页。':'。') : '本页没有素材。';
  catalog.entries.forEach(entry=>{
    const button=document.createElement('button'); button.className='secondary';
    button.textContent=entry.filename+' · 版本 '+entry.revision;
    button.onclick=()=>{ $('filename').value=entry.filename; $('catalog-state').textContent='已选择 '+entry.filename+'，尚未试用或保存。'; };
    $('catalog-items').appendChild(button);
  });
}
function show(job) {
  latest=job; busy=job?.phase==='running'; gates();
  if (!job) return;
  $('details').textContent=JSON.stringify(job,null,2);
  const snap=job.snapshot;
  $('progress').value=snap?.total ? snap.written/snap.total : 0;
  $('message').textContent=(labels[job.phase]||job.phase)+(job.cancel_requested?' · 已请求取消，等待设备结果':'')+(job.error?' · '+job.error:'');
  if (job.phase==='running' || lastSeen===job.id) return;
  lastSeen=job.id;
  const value=job.result;
  if (!value) { if(['disconnected','unauthorized','unsupported'].includes(job.phase)) $('connection').textContent=job.error; if(job.operation?.startsWith('catalog-')) {catalog=null;catalogReceipt=null;selection=null;renderCatalog();gates();} return; }
  if (job.operation==='status') $('connection').textContent=value.ready?'设备报告本地就绪'+(value.busy?'，正在忙碌。':'。'):'设备尚未报告就绪。';
  if (job.operation==='info') $('connection').textContent=`设备版本 ${value.major}.${value.minor}.${value.revision}，构建 ${value.build}`;
  if (job.operation==='task-status') $('task-state').textContent='设备任务：'+({none:'无任务',start:'已开始',progress:'运行中',success:'成功',failure:'失败',canceled:'已取消'}[value.state]||'未知')+(value.expired?' · 已过期，不再提醒':'')+(value.progress===null?'':` · ${value.progress}%`);
  if (job.operation==='trial-status') {trial=value; $('trial-state').textContent='设备试用状态：'+value.state;}
  if (job.operation==='default-status') {catalog=null;catalogReceipt=null;renderCatalog();selection=value; $('default-state').textContent=value.version_known?`设备默认：${value.filename} · 版本 ${value.revision} · 状态 ${value.state}`:'默认配置尚未读入，可提交刷新后再次读取。';}
  if (job.operation==='catalog-status') {catalog=value;selection=null;renderCatalog();}
  if (job.operation?.startsWith('catalog-') && value.accepted) {catalog=null;selection=null;catalogReceipt={selection_epoch:value.epoch,selection_nonce:value.operation_nonce};renderCatalog();}
  if (value.installed) { catalog=null;catalogReceipt=null;renderCatalog(); $('filename').value=value.filename; $('message').textContent='设备确认安装完成；尚未设为默认。'; }
  if (value.accepted) $('message').textContent='设备已受理。请读取相应状态，确认实际结果。';
  if (['failed','canceled','unknown'].includes(value.state)) $('message').textContent=({failed:'设备报告失败',canceled:'设备确认取消',unknown:'结果未知，请先读取状态'}[value.state]);
  gates();
}
async function submit(operation, params={}) {
  const id=crypto.randomUUID().replaceAll('-','');
  if (operation.startsWith('catalog-') || operation.startsWith('default-')) {catalog=null;selection=null;catalogReceipt=null;renderCatalog();}
  busy=true;gates();$('message').textContent='正在提交…';
  try {
    await api('/api/start',{id,operation,params});
    if (operation==='resource-upload') $('receipt').value=id;
    if (operation==='trial-start'||operation==='trial-cancel') trial=null;
    if (operation.startsWith('default-') && operation!=='default-status') {selection=null;catalog=null;catalogReceipt=null;renderCatalog();}
    if (operation.startsWith('catalog-') && operation!=='catalog-status') {catalog=null;selection=null;renderCatalog();}
    show((await api('/api/state')).job);
  } catch(e) { $('message').textContent=e.message+' 请先读取本地任务状态，避免重复提交。'; }
}
function milliseconds(id) { const n=Number($(id).value);if(!Number.isInteger(n)||n<1||n>4294967) throw new Error('请输入有效秒数');return n*1000; }
function guarded(action) { return async()=>{try{await action();}catch(e){$('message').textContent=e.message;}}; }
document.querySelectorAll('[data-op]').forEach(b=>b.addEventListener('click',()=>submit(b.dataset.op)));
async function packData() {
  const file=$('pack').files[0];if(!file||file.size<128||file.size>131072) throw new Error('请选择 128 字节至 128 KiB 的眼睛包');
  const bytes=new Uint8Array(await file.arrayBuffer());let binary='';bytes.forEach(b=>binary+=String.fromCharCode(b));
  return btoa(binary);
}
$('upload').onclick=guarded(async()=>submit('resource-upload',{data:await packData(),ttl_ms:milliseconds('ttl')}));
$('resource-resume').onclick=guarded(async()=>submit('resource-resume',{data:await packData(),receipt_id:$('receipt').value}));
$('resource-cancel').onclick=()=>submit('resource-cancel',{receipt_id:$('receipt').value});
$('cancel').onclick=guarded(async()=>{await api('/api/cancel',{id:latest.id});$('message').textContent='已请求取消，尚待设备确认。';});
$('resource-query').onclick=()=>submit('resource-status',$('receipt').value?{receipt_id:$('receipt').value}:{});
$('trial-start').onclick=guarded(()=>submit('trial-start',{expected_trial_id:trial.id,expression:$('expression').value,ttl_ms:milliseconds('seconds'),...($('filename').value?{pack_filename:$('filename').value}:{})}));
$('trial-cancel').onclick=()=>submit('trial-cancel',{expected_trial_id:trial.id});
$('default-refresh').onclick=()=>submit('default-refresh',{selection_epoch:selection.epoch,expected_selection_id:selection.id});
$('default-set').onclick=()=>submit('default-set',{selection_epoch:selection.epoch,expected_selection_id:selection.id,expected_default_revision:String(selection.revision),pack_filename:$('filename').value});
$('catalog-query').onclick=()=>submit('catalog-status',catalogReceipt||{});
$('catalog-refresh').onclick=()=>submit('catalog-page',{selection_epoch:catalog.epoch,expected_selection_id:catalog.id});
$('catalog-next').onclick=()=>submit('catalog-page',{selection_epoch:catalog.epoch,expected_selection_id:catalog.id,catalog_after:catalog.next_cursor});
$('catalog-cancel').onclick=()=>submit('catalog-cancel',{selection_epoch:catalog.epoch,expected_selection_id:catalog.id});
$('catalog-recover').onclick=()=>submit('catalog-recover',{selection_epoch:catalog.epoch,expected_selection_id:catalog.id});
$('export').onclick=()=>{const url=URL.createObjectURL(new Blob([JSON.stringify(latest,null,2)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download='shaniu-workbench-result.json';a.click();URL.revokeObjectURL(url);};
async function poll(){try{const value=await api('/api/state');show(value.job);if(!value.job)$('message').textContent='工作台已就绪，尚未连接设备。';}catch(e){busy=true;gates();$('message').textContent=e.message;}setTimeout(poll,750);}
gates();poll();
