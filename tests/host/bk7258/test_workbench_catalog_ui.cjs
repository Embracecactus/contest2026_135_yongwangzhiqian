// SPDX-License-Identifier: Apache-2.0
// Run the actual browser controller with external DOM/fetch peers, not a second UI state machine.
const fs=require('fs'), path=require('path'), vm=require('vm'), assert=require('assert');
const root=path.resolve(__dirname,'../../..');
const html=fs.readFileSync(path.join(root,'tools/bk7258/_lib/workbench_web/index.html'),'utf8');
const elements={};
function element(id='') {return {id,value:'',disabled:false,textContent:'',children:[],dataset:{},addEventListener(){},appendChild(x){this.children.push(x);},replaceChildren(...xs){this.children=xs;},files:[]};}
for(const match of html.matchAll(/id="([^"]+)"/g)) elements[match[1]]=element(match[1]);
for(const id of ['catalog-state','catalog-items','catalog-refresh','catalog-next','catalog-cancel','catalog-recover']) assert(elements[id],`BLOCKED_INTERFACE missing ${id}`);
const buttons=[...html.matchAll(/<button[^>]*id="([^"]+)"/g)].map(m=>elements[m[1]]);
const sent=[];
const context={console,URL,Blob,Uint8Array,Number,JSON,Error,crypto:{randomUUID:()=> '01000000-0000-0000-0000-000000000000'},location:{hash:''},sessionStorage:{getItem:()=>''},history:{replaceState(){}},setTimeout(){},document:{getElementById:id=>elements[id],querySelectorAll:selector=>selector==='button'?buttons:[],createElement:()=>element()},fetch:async(url,options)=>{if(options.body)sent.push(JSON.parse(options.body));return {ok:true,json:async()=>({job:null})};}};
vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(root,'tools/bk7258/_lib/workbench_web/app.js'),'utf8'),context);
const run=code=>vm.runInContext(code,context);
let nextId=0;
function show(result,phase='returned'){context.input={id:String(++nextId),operation:'catalog-status',phase,result};run('show(input)');}
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
 context.input={id:'default-known',phase:'returned',operation:'default-status',result:{state:'done',epoch:base.epoch,id:2,version_known:true,revision:2}};run('show(input)');assert(!elements['default-set'].disabled);
 show(base);assert(elements['default-set'].disabled);
 show({...base,more:true,next_cursor:'d.bkep'});context.oldJob=context.input;
 context.fetch=async()=>{throw new Error('lost start response');};
 await run('submit("catalog-page",{selection_epoch:catalog.epoch,expected_selection_id:catalog.id})');
 run('show(oldJob)');assert(elements['catalog-next'].disabled);assert(elements['catalog-refresh'].disabled);assert.equal(elements['catalog-items'].children.length,0);
 console.log('CONTRACT_PASS actual browser catalog gates, selection no write, stale/unknown clears list, shared default invalidation');
})().catch(e=>{console.error(e);process.exit(1)});
