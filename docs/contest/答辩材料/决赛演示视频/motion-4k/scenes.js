// SPDX-License-Identifier: Apache-2.0
/* Deterministic 4K motion design. Logical stage: 1920 × 1080. */
const canvas = document.querySelector('canvas');
const c = canvas.getContext('2d');
const P = {paper:'#F5F0E8',ink:'#252A2D',red:'#CB4139',muted:'#8B8074',line:'#DCD2C6',mint:'#719B8E',white:'#FFFDFA'};
const clamp=(x,a=0,b=1)=>Math.max(a,Math.min(b,x));
const ease=x=>1-Math.pow(1-clamp(x),3);
const smooth=x=>{x=clamp(x);return x*x*(3-2*x)};
const lerp=(a,b,t)=>a+(b-a)*t;
const enter=(t,start,d=.75)=>ease((t-start)/d);
let ctxScene='intro';
const portraitImage=new Image();
portraitImage.src='../../../../../android/shaniu-companion/app/src/main/res/drawable-nodpi/shaniu_launcher_response.webp';
window.filmReady=Promise.all([
  new FontFace('MiSans','url("../../素材/fonts/MiSans-Medium.ttf")').load().then(f=>document.fonts.add(f)),
  portraitImage.decode()
]);

function box(x,y,w,h,r=24,fill=P.white,stroke=null){
  c.beginPath();c.roundRect(x,y,w,h,r);
  if(fill){c.fillStyle=fill;c.fill()}
  if(stroke){c.strokeStyle=stroke;c.lineWidth=1.5;c.stroke()}
}
function text(str,x,y,size=30,color=P.ink,align='left'){
  c.fillStyle=color;c.font=`500 ${size}px MiSans`;c.textAlign=align;c.textBaseline='alphabetic';c.fillText(str,x,y);
}
function line(x1,y1,x2,y2,color=P.line,width=2){c.beginPath();c.moveTo(x1,y1);c.lineTo(x2,y2);c.strokeStyle=color;c.lineWidth=width;c.stroke()}
function dot(x,y,r,color){c.beginPath();c.arc(x,y,r,0,Math.PI*2);c.fillStyle=color;c.fill()}
function group(x,y,s,alpha,fn){c.save();c.translate(x,y);c.scale(s,s);c.globalAlpha*=clamp(alpha);fn();c.restore()}
function panel(x,y,w,h,r=28){
  c.save();c.shadowColor='#5E372318';c.shadowBlur=34;c.shadowOffsetY=16;box(x,y,w,h,r,P.white);c.restore();
  box(x,y,w,h,r,null,'#FFFFFF');
}
function reveal(t,start,x,y,fn,d=.8){const a=enter(t,start,d);group(x,y+32*(1-a),1,a,fn)}
function pill(label,x,y,color=P.red){
  c.font='500 21px MiSans';const w=c.measureText(label).width+40;
  box(x,y,w,42,21,color);text(label,x+20,y+29,21,P.white);return w;
}
function backdrop(t){
  c.fillStyle=P.paper;c.fillRect(0,0,1920,1080);
  let g=c.createRadialGradient(1510,300,20,1510,300,790);g.addColorStop(0,'#E9C1AC58');g.addColorStop(1,'#E9C1AC00');c.fillStyle=g;c.fillRect(0,0,1920,1080);
  g=c.createRadialGradient(1100,920,20,1100,920,750);g.addColorStop(0,'#CCD7C039');g.addColorStop(1,'#CCD7C000');c.fillStyle=g;c.fillRect(0,0,1920,1080);
  c.save();c.strokeStyle='#BCAA9522';c.lineWidth=1;
  for(let i=0;i<4;i++){c.beginPath();c.arc(1420,500,270+i*80,Math.PI*1.13,Math.PI*2.3);c.stroke()}
  c.restore();
  dot(130,82,7,P.red);text('SHANIU  /  傻妞',150,91,22);
  text('OPENVELA AI HARDWARE  ·  FINALS',1790,91,19,P.muted,'right');
  line(130,118,1790,118,'#DCD2C6',1);
}
function nav(active,t){
  const names=['相遇 · 实机','相识 · 手机','决定 · 审批','延伸 · 工作台'];
  const x=130,y=956,w=1660;
  line(x,y,x+w,y,P.line,2);
  const progress=clamp((active+.15+.7*enter(t,.7,3.4))/4);
  line(x,y,x+w*progress,y,P.red,3);
  names.forEach((name,i)=>{dot(x+i*415,y,i===active?6:4,i<=active?P.red:P.line);text(`0${i+1}`,x+i*415,y+45,19,i===active?P.red:P.muted);text(name,x+43+i*415,y+45,23,i===active?P.ink:P.muted)});
}
function smallIcon(name,x,y,s=1,color=P.red){
  group(x,y,s,1,()=>{
    c.strokeStyle=color;c.fillStyle=color;c.lineWidth=3.4;c.lineCap='round';c.lineJoin='round';
    if(name==='check'){c.beginPath();c.moveTo(-15,0);c.lineTo(-4,11);c.lineTo(18,-13);c.stroke()}
    else if(name==='phone'){box(-16,-26,32,52,8,null,color);line(-5,18,5,18,color,2)}
    else if(name==='computer'){box(-29,-22,58,39,6,null,color);line(0,17,0,27,color,3);line(-14,27,14,27,color,3)}
    else if(name==='camera'){box(-28,-18,56,38,8,null,color);box(-13,-26,26,9,3,color);c.beginPath();c.arc(0,1,11,0,Math.PI*2);c.stroke()}
    else if(name==='voice'){for(let i=-2;i<=2;i++)line(i*10,-(20-Math.abs(i)*6),i*10,(20-Math.abs(i)*6),color,5)}
    else if(name==='eyes'){box(-27,-15,22,30,11,color);box(5,-15,22,30,11,color)}
    else if(name==='motion'){c.beginPath();c.moveTo(-30,13);c.lineTo(-16,13);c.lineTo(-5,-19);c.lineTo(7,23);c.lineTo(17,-5);c.lineTo(30,-5);c.stroke()}
    else {box(-21,-21,42,42,8,null,color);for(let i=-12;i<=12;i+=12){line(i,-29,i,-22,color,2);line(i,22,i,29,color,2);line(-29,i,-22,i,color,2);line(22,i,29,i,color,2)}}
  });
}
function face(x,y,w,t,state='awake',rotation=0){
  group(x,y,w/300,1,()=>{
    c.rotate(rotation);
    c.save();c.shadowColor='#5E37232A';c.shadowBlur=28;c.shadowOffsetY=16;
    let g=c.createLinearGradient(0,-90,0,105);g.addColorStop(0,'#FFFFFF');g.addColorStop(1,'#E8E0D5');box(-150,-95,300,190,48,g);c.restore();
    box(-146,-91,292,176,45,null,'#FFFFFF');
    dot(0,-58,5,P.red);
    const phase=((t+1.1)%4.3);const blink=1-.93*Math.exp(-Math.pow((phase-.18)/.075,2));
    for(const xx of [-72,72]){
      dot(xx,5,64,'#C4BEB5');dot(xx,2,60,'#2C3030');dot(xx,2,52,'#101A19');
      c.save();c.translate(xx,2);c.scale(1,blink);
      if(state==='alert'){text('!',0,28,73,'#F4C260','center')}
      else if(state==='happy'){
        c.beginPath();c.arc(0,15,25,Math.PI*1.13,Math.PI*1.87);c.strokeStyle='#BCEBDD';c.lineWidth=13;c.lineCap='round';c.stroke();
      }else{box(-10,-25,20,50,10,'#BCEBDD');dot(-2,-18,3,'#FFFFFF88')}
      c.restore();
    }
    line(-86,80,86,80,'#CEC2B5',2);
  });
}
function portrait(x,y,w,t){
  group(x,y,1,1,()=>{
    c.save();c.shadowColor='#B5714E25';c.shadowBlur=55;
    const g=c.createLinearGradient(-w/2,-w/2,w/2,w/2);g.addColorStop(0,'#EDC2AC');g.addColorStop(1,'#F8E5D9');dot(0,0,w*.47,g);c.restore();
    c.save();c.rotate(-.025+Math.sin(t*.8)*.012);c.drawImage(portraitImage,-w/2,-w*.55,w,w);c.restore();
  });
}
function bubble(label,x,y,t,start){
  const p=enter(t,start,.45);group(x,y+20*(1-p),.9+.1*p,p,()=>{
    panel(-85,-38,170,76,24);text(label,0,12,35,P.ink,'center');
    c.fillStyle=P.white;c.beginPath();c.moveTo(-35,32);c.lineTo(-49,55);c.lineTo(-7,32);c.fill();
  });
}
function flow(x1,y1,x2,y2,p,color=P.red,alpha=1){
  c.save();c.globalAlpha*=alpha;c.lineWidth=2.4;c.strokeStyle=P.line;c.setLineDash([6,7]);
  c.beginPath();c.moveTo(x1,y1);c.bezierCurveTo(lerp(x1,x2,.45),y1,lerp(x1,x2,.55),y2,x2,y2);c.stroke();c.setLineDash([]);
  if(p>0&&p<1){const q=1-p;const xx=q*q*q*x1+3*q*q*p*lerp(x1,x2,.45)+3*q*p*p*lerp(x1,x2,.55)+p*p*p*x2;const yy=q*q*q*y1+3*q*q*p*y1+3*q*p*p*y2+p*p*p*y2;dot(xx,yy,11,color);dot(xx,yy,21,color+'18')}
  c.restore();
}

function intro(t){
  backdrop(t);nav(0,t);
  reveal(t,0,130,244,()=>pill('桌面 AI 伴侣',0,0));
  reveal(t,.15,125,438,()=>text('傻妞',0,0,144));
  reveal(t,.5,135,549,()=>text('把一声「我在」',0,0,55));
  reveal(t,.8,135,625,()=>text('带到你的桌面。',0,0,55,P.red));
  reveal(t,1.2,138,745,()=>text('初赛实拍  ×  决赛新增演示',0,0,27,P.muted));
  text('角色与设备造型示意',138,856,21,P.muted);
  const p=enter(t,.1,1.2);
  group(1370,440+20*Math.sin(t*.75),.9+.1*p,p,()=>portrait(0,0,590,t));
  reveal(t,.65,1365,736,()=>face(0,0,380,t,t>4?'happy':'awake'));
  bubble('我在',1640,360,t,1.6);
  const a=enter(t,3,1);group(1010,510,1,a,()=>{panel(-65,-65,130,130,30);smallIcon('phone',0,0,1.4);text('手机',0,104,24,P.muted,'center')});
  group(1715,650,1,enter(t,3.55),()=>{panel(-65,-65,130,130,30);smallIcon('computer',0,0,1.2);text('电脑',0,104,24,P.muted,'center')});
  flow(1090,530,1170,718,((t-3.2)/1.3)%1,P.red,a);
  flow(1555,735,1643,660,((t-4.3)/1.3)%1,P.red,enter(t,4.1));
}

function app(t){
  backdrop(t);nav(1,t);
  reveal(t,0,130,244,()=>pill('02  /  相识',0,0));
  reveal(t,.1,130,420,()=>text('先让她',0,0,105));
  reveal(t,.3,130,550,()=>text('认识你。',0,0,105,P.red));
  reveal(t,.7,135,657,()=>text('认领、连接，留下你的习惯。',0,0,33,P.muted));
  text('交互流程示意  ·  接下来为初赛 App 实拍',135,854,23,P.muted);
  const a=enter(t,.15,.7);
  group(1430,526+Math.sin(t)*5,.92+.08*a,a,()=>{
    panel(-183,-332,366,664,48);box(-171,-321,342,642,39,'#F7F2EC','#DED3C7');box(-52,-305,104,24,12,'#272A2A');
    c.drawImage(portraitImage,-66,-238,132,132);text('你好，傻妞',0,-74,31,P.ink,'center');
    ['认领我的设备','连接网络与云服务','设置喜欢的表情'].forEach((label,i)=>{
      const y=4+i*89,p=enter(t,1+i*.7,.4);
      box(-141,y-32,282,70,17,P.white);text(label,-115,y+10,23);
      dot(112,y+2,15,p>.5?P.red:P.line);if(p>.5)smallIcon('check',112,y+2,.42,P.white);
    });
  });
  group(1005,625,1,enter(t,.8),()=>face(0,0,260,t,'happy',Math.sin(t*.8)*.04));
  flow(1136,620,1235,475,((t-1.2)/1.6)%1,P.red,enter(t,1));
}

function computer(x,y,t,done=false){
  panel(x-172,y-112,344,224,20);box(x-159,y-98,318,193,11,'#282D2E');
  for(let i=0;i<3;i++)dot(x-140+i*14,y-79,3.3,i===0?P.red:'#8B9895');
  if(done){box(x-112,y-46,224,109,10,'#F8F4EE');text('0',x+88,y-4,36,P.ink,'right');for(let i=0;i<3;i++)for(let j=0;j<5;j++)box(x-93+j*39,y+11+i*14,28,8,2,i===2&&j===4?P.red:'#DFD9D0')}
  else{text('> request: calculator',x-129,y-20,20,'#E5EAE7');text(t>1?'waiting for approval…':'ready',x-129,y+22,18,t>1?'#F0BA7A':'#9BA7A2');}
  line(x,y+113,x,y+147,'#7A7970',8);box(x-55,y+144,110,10,4,'#AEA79D');
}
function approval(t){
  backdrop(t);nav(2,t);
  reveal(t,0,130,240,()=>pill('03  /  决定',0,0));
  reveal(t,.1,130,408,()=>text('每一步执行',0,0,87));
  reveal(t,.3,130,520,()=>text('先听你的。',0,0,87,P.red));
  reveal(t,.8,135,622,()=>text('电脑发起请求，手机作出决定。',0,0,31,P.muted));
  text('审批流程示意  ·  实际批准过程见后续实录',135,854,22,P.muted);
  reveal(t,.2,980,545,()=>computer(0,0,t,t>5.5));
  reveal(t,.5,1307,555,()=>face(0,0,185,t,t>1.5&&t<4.9?'alert':'happy'));
  reveal(t,.75,1600,500,()=>{
    panel(-120,-208,240,416,34);box(-110,-198,220,396,28,'#F8F4EE');box(-34,-187,68,14,7,P.ink);
    text('操作审批',0,-126,27,P.ink,'center');
    const a=enter(t,2.6,.5);group(0,0,1,a,()=>{
      box(-90,-81,180,117,15,P.white);smallIcon('computer',0,-43,.65);text('打开计算器',0,13,23,P.ink,'center');
      box(-90,69,180,57,17,t>4.5?P.mint:P.red);text(t>4.5?'已批准':'批准执行',0,106,24,P.white,'center');
    });
  });
  flow(1160,540,1206,555,clamp((t-1)/.75),P.red,enter(t,.7));
  flow(1410,555,1470,500,clamp((t-2)/.75),P.red,enter(t,1.7));
  if(t>3.65&&t<4.9){const z=smooth((t-3.65)/.7);group(lerp(1740,1616,z),lerp(758,609,z),1,1,()=>{c.fillStyle=P.ink;c.beginPath();c.moveTo(0,0);c.lineTo(4,31);c.lineTo(12,21);c.lineTo(24,18);c.closePath();c.fill()});}
  if(t>4.5){const p=clamp((t-4.5)/.5);c.save();c.globalAlpha=1-p;c.strokeStyle=P.red;c.lineWidth=3;c.beginPath();c.arc(1600,598,20+p*45,0,Math.PI*2);c.stroke();c.restore();}
  flow(1580,748,985,754,clamp((t-4.7)/.8),P.mint,enter(t,4.6));
  [['电脑',980],['傻妞',1307],['手机',1600]].forEach(([s,x])=>text(s,x,822,25,P.muted,'center'));
  if(t>5.5)reveal(t,5.5,980,375,()=>{pill('批准后执行',-77,0,P.mint)});
}

function workbench(t){
  backdrop(t);nav(3,t);
  reveal(t,0,130,242,()=>pill('04  /  延伸',0,0));
  reveal(t,.1,130,418,()=>text('陪伴，延伸到',0,0,85));
  reveal(t,.3,130,532,()=>text('电脑桌面。',0,0,85,P.red));
  reveal(t,.7,135,642,()=>text('换一双眼睛。感知动作。采一帧。',0,0,30,P.muted));
  text('功能动效示意  ·  接下来为工作台实录',135,854,23,P.muted);
  const phase=t<1.7?0:t<3.15?1:2;
  group(1380,532,1,enter(t,.2),()=>{
    panel(-385,-259,770,518,30);line(-385,-194,385,-194,P.line,1);
    for(let i=0;i<3;i++)dot(-351+i*18,-227,4,i===0?P.red:P.line);
    ['表情','动作','取景'].forEach((label,i)=>{box(-223+i*175,-174,151,46,23,i===phase?P.red:'#F4EEE7');text(label,-148+i*175,-142,23,i===phase?P.white:P.muted,'center')});
    if(phase<2){face(0,47,340,t,phase===0?'happy':'awake',phase===1?Math.sin((t-1.7)*4)*.16:0);text(phase===0?'换上喜欢的眼睛':'动作，让表情跟着变化',0,205,26,P.muted,'center');}
    else{
      box(-170,-81,340,224,23,'#EBE3D7');
      const a=enter(t,3.4,.45);
      group(0,25,1,a,()=>{c.fillStyle='#CBCFBB';c.beginPath();c.moveTo(-156,101);c.lineTo(-50,-35);c.lineTo(25,40);c.lineTo(83,-10);c.lineTo(156,101);c.fill();dot(95,-53,22,'#D9A16B');});
      const corners=[[-170,-81,1,1],[170,-81,-1,1],[-170,143,1,-1],[170,143,-1,-1]];
      corners.forEach(([x,y,dx,dy])=>{line(x,y,x+35*dx,y,P.red,5);line(x,y,x,y+35*dy,P.red,5)});
      text(t>3.8?'一帧图像，按需查看':'采一帧',0,205,26,P.muted,'center');
      if(t<3.7){c.fillStyle=`rgba(255,255,255,${Math.max(0,1-(t-3.15)/.55)})`;c.fillRect(-170,-81,340,224)}
    }
  });
}

const architectureCaptions=[
 [0,1.766,'这三块板的背后共用的'],[1.766,3.933,'是同一套 BK7258 芯片实现'],
 [4.066,5.4,'在这套架构里面'],[5.4,7.2,'CPU0 运行 CP 系统'],
 [7.333,10.733,'CPU1 和 CPU2 运行 AP 的双核 SMP 系统'],[11.133,12.766,'两边通过核间通信协作'],
 [13,14.866,'所以它不是一个单镜像系统'],[14.866,17.5,'而是 CP 和 AP 两份固件配合运行']
];
function architecture(t){
  backdrop(t);
  text('架构示意 · 初赛讲解原声',1790,176,21,P.muted,'right');
  reveal(t,0,130,235,()=>text('一颗芯片，分工协作。',0,0,67));
  reveal(t,.25,135,293,()=>text('1 颗 BK7258  /  2 个 openvela 实例  /  3 个 CPU',0,0,28,P.muted));
  const a=enter(t,.5);
  group(0,0,1,a,()=>{
    panel(180,350,1560,502,38);text('BK7258',224,402,24,P.muted);
    const cp=enter(t,5.25,.5),ap=enter(t,7.3,.5);
    box(260,449,480,277,27,cp>.1?'#F8E7DF':'#F2EEE7');
    box(260,449,480,277,27,null,cp>.1?P.red:P.line);
    text('CP',300,511,39,cp>.1?P.red:P.muted);
    text('openvela 实例',300,553,23,P.muted);
    box(300,596,397,89,17,P.white);text('CPU0',498,653,34,P.ink,'center');
    box(1070,449,570,277,27,ap>.1?'#E6EEE7':'#F2EEE7');
    box(1070,449,570,277,27,null,ap>.1?P.mint:P.line);
    text('AP  ·  SMP',1110,511,39,ap>.1?P.mint:P.muted);text('openvela 实例',1110,553,23,P.muted);
    [1218,1490].forEach((x,i)=>{box(x-110,596,220,89,17,P.white);text(`CPU${i+1}`,x,653,34,P.ink,'center')});
    const ipc=enter(t,11,.5);
    group(0,0,1,ipc,()=>{flow(758,573,1052,573,((t-11)/1.1)%1,P.red);text('RPMsg',906,537,28,P.ink,'center');text('核间通信',906,625,23,P.muted,'center')});
    group(0,0,1,enter(t,14.7,.6),()=>{pill('CP 固件',425,778,P.red);pill('AP 固件',1275,778,P.mint);text('配合运行',920,810,24,P.muted,'center');line(590,799,819,799);line(1018,799,1260,799)});
  });
  const cue=architectureCaptions.find(([a,b])=>t>=a&&t<b);
  if(cue){text(cue[2],960,977,36,P.ink,'center');}
  text('结构动画用于说明分工，不是实时运行遥测。',960,1033,19,P.muted,'center');
}

function outro(t){
  backdrop(t);
  group(490,464+Math.sin(t)*7,1,enter(t,0),()=>portrait(0,0,500,t));
  reveal(t,.3,490,737,()=>face(0,0,290,t,'happy'));
  reveal(t,.35,865,415,()=>text('把回应带到桌面。',0,0,72));
  reveal(t,.7,865,524,()=>text('把决定留给你。',0,0,72,P.red));
  reveal(t,1.2,870,648,()=>text('傻妞  /  桌面 AI 伴侣',0,0,31,P.muted));
  reveal(t,1.6,870,718,()=>text('勇往直前',0,0,25,P.muted));
  bubble('我在',700,300,t,1.1);
  line(130,938,1790,938,P.line,1);
  text('open-vela / contest2026_135_yongwangzhiqian',960,1000,24,P.muted,'center');
}
window.setScene=name=>{ctxScene=name};
window.render=(t)=>{
  c.setTransform(1,0,0,1,0,0);c.clearRect(0,0,canvas.width,canvas.height);
  c.scale(canvas.width/1920,canvas.height/1080);c.lineCap='round';c.lineJoin='round';c.globalAlpha=1;
  ({intro,app,approval,workbench,architecture,outro}[ctxScene]||intro)(t);
  window.animationFinished=true;
};
window.filmReady.then(()=>window.render(0));
