// SPDX-License-Identifier: Apache-2.0
// Local, deterministic Canvas frames → NVENC. No editor UI or external service.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';
import {parseArgs} from 'node:util';
import {spawn} from 'node:child_process';
const here=path.dirname(fileURLToPath(import.meta.url));
const require=createRequire(process.env.PLAYWRIGHT_PACKAGE || import.meta.url);
const {chromium}=require('playwright');
const {values:a}=parseArgs({options:{scene:{type:'string'},time:{type:'string',default:'3'},output:{type:'string'},duration:{type:'string'},width:{type:'string',default:'3840'},height:{type:'string',default:'2160'},batch:{type:'boolean'},fps:{type:'string',default:'30'}}});
const spec=JSON.parse(fs.readFileSync(path.join(here,'edit.json'),'utf8'));
const browserPath=process.env.CHROME_BIN || chromium.executablePath();
const browser=await chromium.launch({executablePath:browserPath,headless:true,args:['--allow-file-access-from-files','--disable-gpu']});
const page=await browser.newPage({viewport:{width:1920,height:1080}});
page.on('pageerror',e=>console.error(e));
await page.goto(pathToFileURL(path.join(here,'index.html')).href);
await page.evaluate(()=>window.filmReady);
await page.evaluate(([w,h])=>{const canvas=document.querySelector('canvas');canvas.width=w;canvas.height=h},[Number(a.width),Number(a.height)]);
async function frame(scene,t,mime='image/jpeg'){
  const data=await page.evaluate(([scene,t,mime])=>{window.setScene(scene);window.render(t);return document.querySelector('canvas').toDataURL(mime,.97)},[scene,t,mime]);
  return Buffer.from(data.split(',')[1],'base64');
}
async function video(scene,duration,output){
  const fps=Number(a.fps),n=Math.round(duration*fps);
  fs.mkdirSync(path.dirname(output),{recursive:true});
  const partial=output.replace(/\.mp4$/,'.partial.mp4');
  const enc=spawn('ffmpeg',['-hide_banner','-v','error','-y','-f','image2pipe','-framerate',String(fps),'-vcodec','mjpeg','-i','pipe:0','-an','-c:v','h264_nvenc','-preset','p5','-rc','vbr','-cq','19','-b:v','0','-maxrate','38M','-bufsize','76M','-pix_fmt','yuv420p','-profile:v','high','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709','-video_track_timescale','30000','-movflags','+faststart',partial],{stdio:['pipe','ignore','pipe']});
  let errors='';enc.stderr.on('data',b=>errors=(errors+b).slice(-6000));
  const ended=new Promise((resolve,reject)=>{enc.once('error',reject);enc.once('exit',code=>code===0?resolve():reject(Error(errors||`ffmpeg exit ${code}`)))});
  enc.stdin.on('error',()=>{});
  try{
    for(let i=0;i<n;i++){
      const b=await frame(scene,i/fps);
      await new Promise((resolve,reject)=>enc.stdin.write(b,e=>e?reject(e):resolve()));
      if(i%150===0)console.log(`${scene}: ${i}/${n}`);
    }
    enc.stdin.end();await ended;fs.renameSync(partial,output);console.log(`saved ${output}`);
  }catch(e){enc.kill();await ended.catch(()=>{});throw e}
}
try{
  if(a.batch){
    for(const row of spec.timeline.filter(r=>r.scene)){
      const out=path.join(here,'assets',`${row.scene}-4k.mp4`);
      if(fs.existsSync(out)){console.log(`reuse ${row.scene}`);continue}
      await video(row.scene,row.duration ?? row.out-row.in,out);
    }
  }else if(a.duration){await video(a.scene,Number(a.duration),path.resolve(a.output))}
  else{
    fs.mkdirSync(path.dirname(path.resolve(a.output)),{recursive:true});
    fs.writeFileSync(a.output,await frame(a.scene,Number(a.time),'image/png'));
    console.log(`saved ${a.output}`);
  }
}finally{await browser.close()}
