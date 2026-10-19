// SPDX-License-Identifier: Apache-2.0
// Run the actual browser controller with external DOM/fetch peers, not a second UI state machine.
const fs=require('fs'), path=require('path'), vm=require('vm'), assert=require('assert');
const root=path.resolve(__dirname,'../../..');
const html=fs.readFileSync(path.join(root,'tools/bk7258/_lib/workbench_web/index.html'),'utf8');
const elements={};
function element(id='') {return {id,value:'',disabled:false,textContent:'',children:[],dataset:{},addEventListener(){},appendChild(x){this.children.push(x);},replaceChildren(...xs){this.children=xs;},files:[]};}
for(const match of html.matchAll(/id="([^"]+)"/g)) elements[match[1]]=element(match[1]);
for(const id of ['catalog-state','catalog-items','catalog-refresh','catalog-next','catalog-cancel','catalog-recover','default-query','default-check','default-cancel','default-recover']) assert(elements[id],`BLOCKED_INTERFACE missing ${id}`);
const buttons=[...html.matchAll(/<button[^>]*id="([^"]+)"/g)].map(m=>elements[m[1]]);
const sent=[];
const context={console,URL,Blob,Uint8Array,Number,JSON,Error,crypto:{randomUUID:()=> '01000000-0000-0000-0000-000000000000'},location:{hash:''},sessionStorage:{getItem:()=>''},history:{replaceState(){}},setTimeout(){},document:{getElementById:id=>elements[id],querySelectorAll:selector=>selector==='button'?buttons:[],createElement:()=>element()},fetch:async(url,options)=>{if(options.body)sent.push(JSON.parse(options.body));return {ok:true,json:async()=>({job:null})};}};
vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(root,'tools/bk7258/_lib/workbench_web/app.js'),'utf8'),context);
const run=code=>vm.runInContext(code,context);
let nextId=0;
function show(result,phase='returned'){context.input={id:String(++nextId),operation:'catalog-status',phase,result};run('show(input)');}
function showDefault(result,phase='returned'){context.input={id:String(++nextId),operation:'default-status',phase,result};run('show(input)');}
const base={state:'done',epoch:'07'+'00'.repeat(15),id:1,page_available:true,entries:[{filename:'a.bkep',pack_id:'a',revision:1}],more:false,catalog:true,release_error:0};
(async()=>{
 await Promise.resolve();await Promise.resolve();
 show({...base,state:'pending',page_available:false,entries:[]});
 assert.equal(elements['catalog-items'].children.length,0);assert(elements['catalog-cancel'].disabled===false);assert(elements['catalog-refresh'].disabled);assert(!elements['catalog-state'].textContent.includes('没有'));
 show(base);assert.equal(elements['catalog-items'].children.length,1);assert(elements['catalog-next'].disabled);
 const count=sent.length;elements['catalog-items'].children[0].onclick();assert.equal(elements.filename.value,'a.bkep');assert.equal(sent.length,count);
 show({...base,more:true,next_cursor:'d.bkep'});assert(!elements['catalog-next'].disabled);
 show(null,'unconfirmed');assert(elements['catalog-next'].disabled);assert(elements['catalog-refresh'].disabled);assert.equal(elements['catalog-items'].children.length,0);
 show({...base,state:'unknown',page_available:false,entries:[],release_error:-5});assert(!elements['catalog-recover'].disabled);assert(elements['catalog-refresh'].disabled);
 show({...base,entries:[]});assert(elements['catalog-state'].textContent.includes('没有'));assert(!elements['catalog-refresh'].disabled);
 const known={state:'done',epoch:base.epoch,id:2,version_known:true,revision:'18446744073709551615',filename:'a.bkep',refresh_complete:true,catalog:false,release_error:0};
 showDefault(known);assert(!elements['default-set'].disabled);
 await elements['default-set'].onclick();
 assert.equal(sent.at(-1).operation,'default-set');
 assert.equal(sent.at(-1).params.expected_default_revision,'18446744073709551615');
 assert(!elements['default-check'].disabled);
 const submitted=sent.at(-1);
 await elements['default-check'].onclick();
 assert.equal(sent.at(-1).operation,'default-status');
 assert.deepEqual(sent.at(-1).params,{selection_epoch:known.epoch,selection_nonce:submitted.id});
 // A stale or failed query must not enable a write using an earlier snapshot.
 showDefault(null,'unconfirmed');assert(elements['default-set'].disabled);assert(elements['default-refresh'].disabled);assert(!elements['default-check'].disabled);
 for(const state of ['pending','preparing','committing','rendering','cancel_pending']) {
   showDefault({...known,state,refresh_complete:false});
   assert(elements['default-set'].disabled);assert(elements['default-refresh'].disabled);
   assert.equal(elements['default-cancel'].disabled,!['pending','preparing'].includes(state));
 }
 showDefault({...known,state:'pending',refresh_complete:false});
 await elements['default-cancel'].onclick();
 assert.equal(sent.at(-1).operation,'default-cancel');
 assert.deepEqual(sent.at(-1).params,{selection_epoch:known.epoch,expected_selection_id:known.id});
 assert(elements['default-state'].textContent.includes('待核对'));
 showDefault({...known,state:'unknown',refresh_complete:false,device_reports_saved:true,device_reports_rendered:false,release_error:-5});
 assert(elements['default-set'].disabled);assert(elements['default-refresh'].disabled);assert(!elements['default-recover'].disabled);
 assert(elements['default-state'].textContent.includes('已保存'));assert(elements['default-state'].textContent.includes('画面更新未确认'));
 await elements['default-recover'].onclick();assert.equal(sent.at(-1).operation,'default-recover');
 assert.deepEqual(sent.at(-1).params,{selection_epoch:known.epoch,expected_selection_id:known.id});
 showDefault({...known,state:'unknown',refresh_complete:false,release_error:-5,recovery_pending:true});assert(elements['default-recover'].disabled);
 showDefault({...known,state:'unknown',refresh_complete:false});
 assert(elements['default-set'].disabled);assert(elements['default-recover'].disabled);assert(!elements['default-refresh'].disabled);assert(elements['default-state'].textContent.includes('结果未知'));
 await elements['default-query'].onclick();assert.deepEqual(sent.at(-1).params,{});assert(elements['default-check'].disabled);
 showDefault({...known,catalog:true,version_known:false,refresh_complete:false});assert(elements['default-set'].disabled);assert(elements['default-state'].textContent.includes('目录作业'));
 showDefault(known);
 show(base);assert(elements['default-set'].disabled);
 show({...base,more:true,next_cursor:'d.bkep'});context.oldJob=context.input;
 context.fetch=async()=>{throw new Error('lost start response');};
 await run('submit("catalog-page",{selection_epoch:catalog.epoch,expected_selection_id:catalog.id})');
 run('show(oldJob)');assert(elements['catalog-next'].disabled);assert(elements['catalog-refresh'].disabled);assert.equal(elements['catalog-items'].children.length,0);
 // A lost response must retain the request identity without replaying the write.
 showDefault(known);context.oldJob=context.input;
 await run('submit("default-set",{selection_epoch:selection.epoch,expected_selection_id:selection.id,expected_default_revision:String(selection.revision),pack_filename:"a.bkep"})');
 run('show(oldJob)');assert(elements['default-set'].disabled);assert(!elements['default-check'].disabled);
 context.fetch=async(url,options)=>{if(options.body)sent.push(JSON.parse(options.body));return {ok:true,json:async()=>({job:null})};};
 await elements['default-check'].onclick();assert.equal(sent.at(-1).operation,'default-status');assert.equal(sent.at(-1).params.selection_epoch,known.epoch);assert.equal(sent.at(-1).params.selection_nonce,'01000000000000000000000000000000');
 console.log('CONTRACT_PASS actual browser catalog/default gates, bound result queries, cancel/release-only recovery, stale/unknown invalidation, lost ACK without replay');
})().catch(e=>{console.error(e);process.exit(1)});
