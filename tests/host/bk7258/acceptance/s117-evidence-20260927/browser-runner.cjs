const {chromium}=require('/tmp/shaniu-browser-check/node_modules/playwright');
const fs=require('fs'),assert=require('assert');
(async()=>{
 const url=fs.readFileSync('out/shaniu-s117/browser-private.log','utf8').match(/http:\/\/127\.0\.0\.1:\d+\/#\S+/)[0];
 const browser=await chromium.launch({executablePath:'/home/lijian/.cache/ms-playwright/chromium-1234/chrome-linux64/chrome',headless:true});
 const page=await browser.newPage({viewport:{width:1280,height:1000}}),errors=[],ops=[];
 page.on('pageerror',e=>errors.push(e.message));
 let job=null,scanned=false,count=0;
 const idle={state:'idle',id:0,epoch:'07'+'00'.repeat(15),catalog:false,release_error:0,page_available:false,more:false,entries:[]};
 await page.route('**/api/**',async route=>{
  if(route.request().url().endsWith('/api/start')){
   const req=route.request().postDataJSON();ops.push(req.operation);
   if(req.operation==='catalog-page'){scanned=true;job={id:String(++count),operation:req.operation,phase:'returned',result:{accepted:true,epoch:idle.epoch,operation_nonce:req.id}};}
   else if(req.operation==='catalog-status') job={id:String(++count),operation:req.operation,phase:'returned',result:scanned?{...idle,state:'done',id:1,catalog:true,page_available:true,entries:[{filename:'shaniu-default-v1.bkep',revision:1},{filename:'shaniu-very-long-example-resource-v1.bkep',revision:2}]}:idle};
   await route.fulfill({json:{id:req.id}});
  }else await route.fulfill({json:{job}});
 });
 await page.goto(url);await page.getByText('工作台已就绪，尚未连接设备。',{exact:true}).waitFor();
 assert(await page.locator('#catalog-refresh').isDisabled());
 await page.locator('#catalog-query').click();await page.getByText(/目录状态：idle/).waitFor();
 await page.locator('#catalog-refresh').click();await page.getByText('设备已受理。请读取相应状态，确认实际结果。',{exact:true}).waitFor();
 assert.equal(await page.locator('#catalog-items button').count(),0);
 await page.locator('#catalog-query').click();await page.getByText('本页 2 项素材。',{exact:true}).waitFor();
 const before=ops.length;await page.locator('#catalog-items button').first().focus();await page.keyboard.press('Enter');
 assert.equal(await page.locator('#filename').inputValue(),'shaniu-default-v1.bkep');assert.equal(ops.length,before);
 assert(await page.locator('#default-set').isDisabled());
 await page.screenshot({path:'out/shaniu-s117/workbench-desktop.png',fullPage:true});
 await page.setViewportSize({width:360,height:800});await page.emulateMedia({colorScheme:'dark'});
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 await page.screenshot({path:'out/shaniu-s117/workbench-mobile-dark.png',fullPage:true});
 assert.deepEqual(errors,[]);
 console.log(JSON.stringify({PASS:true,browser:browser.version(),operations:ops,checks:['real DOM explicit catalog flow','accepted no list','keyboard selection without network write','default save stays disabled','360px dark no overflow'],limits:'Synthetic HTTP responses for UI only. Separate real HTTP/TLS/native tests; no serial or board operation.'}));
 await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
