// Real HTTP regression test; --webkit exercises the Safari engine.
const {chromium,webkit}=require('playwright');
const http=require('node:http'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const useWebKit=process.argv.includes('--webkit');
(async()=>{
 let browser,viewer,video,corrupt=false;
 const requests=[],reports=[];
 try {
  browser=await (useWebKit?webkit:chromium).launch(useWebKit?{headless:true}:{headless:true,executablePath:process.env.CHROME_PATH||'/usr/bin/google-chrome',args:['--no-sandbox']});
  const page=await browser.newPage({viewport:{width:900,height:480}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const fixtures=await page.evaluate(()=>[320,640].map(width=>{const c=document.createElement('canvas');c.width=width;c.height=width*3/4;const x=c.getContext('2d');x.fillStyle='#d42f21';x.fillRect(0,0,c.width,c.height);return c.toDataURL('image/jpeg').split(',')[1]}));
  video=http.createServer((req,res)=>{
   const url=new URL(req.url,'http://localhost'),binary=url.searchParams.get('format')==='bytes';
   if(binary)requests.push(url);
   res.useChunkedEncodingByDefault=false;
   res.writeHead(200,{'Content-Type':binary?'application/octet-stream':'multipart/x-mixed-replace;boundary=frame','Access-Control-Allow-Origin':'*','Cache-Control':'no-store','Connection':'close'});
   const jpg=corrupt?Buffer.alloc(3346):Buffer.from(fixtures[url.searchParams.get('size')==='vga'?1:0],'base64');
   // Actual failed OV2640 capture: APP0 length 0x8090 exceeds the whole frame.
   if(corrupt){Buffer.from('ffd8ffe08090cac6c9c680818181808080808080','hex').copy(jpg);jpg[jpg.length-2]=0xff;jpg[jpg.length-1]=0xd9}
   const header=Buffer.from(`\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpg.length}\r\n\r\n`),packet=Buffer.concat([header,jpg]);
   let offset=0;
   const timer=setInterval(()=>{const size=offset<header.length?7:311;res.write(packet.subarray(offset,offset+size));offset+=size;if(offset>=packet.length)offset=0},3);
   res.on('close',()=>clearInterval(timer));
  });
  await new Promise(r=>video.listen(0,'127.0.0.1',r));
  const html=fs.readFileSync(path.join(__dirname,'../main/web/index.html'),'utf8').replace("url.port='81'",`url.port='${video.address().port}'`);
  viewer=http.createServer((req,res)=>{res.setHeader('Cache-Control','no-store');if(req.url==='/api/client-error'){let body='';req.on('data',chunk=>body+=chunk);req.on('end',()=>{reports.push(JSON.parse(body));res.writeHead(204);res.end()});return}if(req.url==='/api/status'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify({camera:true,mode:'hotspot',ssid:'Camera-D6A4',ip:'192.168.4.1',clients:1,psram:8388608}));return}res.setHeader('Content-Type','text/html');res.end(req.url==='/probe'?'<!doctype html><title>Probe</title>':html)});
  await new Promise(r=>viewer.listen(0,'127.0.0.1',r));
  const base=`http://127.0.0.1:${viewer.address().port}`;
  if(useWebKit){
   await page.goto(base+'/probe');
   const result=await page.evaluate(async url=>{const c=new AbortController();let timedOut=false;const timeout=setTimeout(()=>{timedOut=true;c.abort()},3000);try{const r=await fetch(url,{signal:c.signal});await r.body.getReader().read();return {rejected:false}}catch(e){return {rejected:true,timedOut,message:e.message}}finally{clearTimeout(timeout);c.abort()}},`http://127.0.0.1:${video.address().port}/stream?size=qvga`);
   assert(result.rejected&&!result.timedOut,'reproduce WebKit rejection of fetch multipart stream');
   console.log('REPRODUCED: WebKit rejects the old multipart fetch:',result.message);
  }
  const received=page.waitForResponse(r=>new URL(r.url()).searchParams.get('format')==='bytes');
  await page.goto(base+'/');
  const response=await received;
  assert.equal(response.headers()['content-type'],'application/octet-stream');
  assert.equal(response.headers()['transfer-encoding'],undefined,'match device close-delimited response');
  assert.equal((await response.headersArray()).filter(h=>h.name.toLowerCase()==='access-control-allow-origin').length,1);
  await page.waitForFunction(()=>document.getElementById('state').textContent==='实时');
  const pixels=await page.evaluate(()=>{const c=document.getElementById('view'),x=c.getContext('2d');return [.25,.75].map(f=>Array.from(x.getImageData(c.width*f,c.height/2,1,1).data))});
  assert(pixels.every(p=>p[0]>150&&p[1]<100),'both eyes render the same decoded frame');
  assert.equal(requests.length,1);assert.equal(requests[0].searchParams.get('quality'),'fast');
  // Real decoded frames while rotating/resizing, including browser bars taking
  // away landscape height. Toolbar wrapping must never push the footer away.
  for(const [width,height] of [[320,568],[390,844],[844,390],[667,375],[740,280],[900,480]]){
   await page.setViewportSize({width,height});
   await page.waitForFunction(()=>{const c=document.getElementById('view');return c.width===Math.round(c.clientWidth*Math.min(devicePixelRatio,2))&&c.height===Math.round(c.clientHeight*Math.min(devicePixelRatio,2))});
   const layout=await page.evaluate(()=>{
    const rect=el=>{const r=el.getBoundingClientRect();return {left:r.left,top:r.top,right:r.right,bottom:r.bottom,width:r.width,height:r.height}};
    return {width:innerWidth,height:innerHeight,scrollWidth:document.documentElement.scrollWidth,scrollHeight:document.documentElement.scrollHeight,video:rect(canvas),footer:rect(document.querySelector('footer')),controls:[...document.querySelectorAll('header button,header select')].map(rect)};
   });
   assert(layout.scrollWidth<=width+1&&layout.scrollHeight<=height+1,`${width}x${height}: page overflows ${JSON.stringify(layout)}`);
   assert(layout.footer.bottom<=height+1&&layout.video.height>=100,`${width}x${height}: footer or video not fully visible`);
   assert(layout.controls.every(r=>r.left>=0&&r.right<=width+1&&r.top>=0&&r.bottom<=layout.video.top+1),`${width}x${height}: toolbar clipped`);
   assert.equal(await page.locator('#state').innerText(),'实时','resizing preserves playback');
  }
  await page.click('#full');
  await page.waitForFunction(()=>document.getElementById('stage').classList.contains('expanded'));
  async function expandedFits(){
   await page.waitForFunction(()=>canvas.width===Math.round(canvas.clientWidth*Math.min(devicePixelRatio,2))&&canvas.height===Math.round(canvas.clientHeight*Math.min(devicePixelRatio,2)));
   const full=await page.evaluate(()=>{const r=canvas.getBoundingClientRect(),exit=document.getElementById('exit-view').getBoundingClientRect();return {left:r.left,top:r.top,right:r.right,bottom:r.bottom,height:r.height,exitTop:exit.top,exitBottom:exit.bottom,exitRight:exit.right,width:innerWidth,viewportHeight:innerHeight}});
   assert(full.left>=-1&&full.top>=-1&&full.right<=full.width+1&&full.bottom<=full.viewportHeight+1&&full.height>100,'expanded canvas stays inside viewport');
   assert(full.exitTop>=full.bottom-1&&full.exitBottom<=full.viewportHeight+1&&full.exitRight<=full.width+1,'exit button remains visible without covering video/HUD');
   assert.equal(await page.locator('#state').innerText(),'实时','expanded view preserves playback');
  }
  await expandedFits();
  await page.click('#exit-view');
  await page.waitForFunction(()=>!document.fullscreenElement&&!document.webkitFullscreenElement&&!document.body.classList.contains('immersive'));
  for(const failure of ['missing','denied']){
   await page.evaluate(failure=>{
    stage.webkitRequestFullscreen=undefined;
    window.fullscreenAttempts=0;
    stage.requestFullscreen=failure==='missing'?undefined:()=>{window.fullscreenAttempts++;return Promise.reject(new DOMException('Fullscreen denied','NotAllowedError'))};
    Object.defineProperty(document,'fullscreenEnabled',{configurable:true,value:true});
   },failure);
   await page.click('#full');
   await page.waitForFunction(()=>document.body.classList.contains('immersive'));
   assert.equal(await page.locator('header').isVisible(),false);
   assert.equal(await page.locator('footer').isVisible(),false);
   assert.match(await page.locator('#view-mode').innerText(),/地址栏/,'do not promise to remove browser chrome');
   if(failure==='denied')assert.equal(await page.evaluate(()=>window.fullscreenAttempts),1);
   for(const [width,height] of [[844,390],[390,844],[740,280]]){await page.setViewportSize({width,height});await expandedFits()}
   if(failure==='missing')await page.click('#exit-view');else await page.keyboard.press('Escape');
   await page.waitForFunction(()=>!document.body.classList.contains('immersive'));
   assert.equal(await page.locator('header').isVisible(),true);
   assert.equal(await page.locator('footer').isVisible(),true);
   assert.equal(requests.length,1,'fallback keeps the existing video connection');
   await page.evaluate(()=>{delete stage.requestFullscreen;delete stage.webkitRequestFullscreen;delete document.fullscreenEnabled;syncViewMode()});
  }
  await page.setViewportSize({width:900,height:480});
  await page.click('#vr');assert.equal(requests.length,1,'VR uses one video connection');
  await page.click('#play');await page.waitForFunction(()=>document.getElementById('play').textContent==='开始观看');
  await page.selectOption('#quality','fine');await page.selectOption('#size','vga');await page.click('#play');
  await page.waitForFunction(()=>document.getElementById('stats').textContent.includes('640 × 480'));
  assert.equal(requests.length,2);assert.equal(requests[1].searchParams.get('quality'),'fine');
  await page.click('#play');await page.waitForFunction(()=>document.getElementById('play').textContent==='开始观看');
  await page.evaluate(()=>{
   window.bitmapAttempts=window.blobCreated=window.blobRevoked=0;
   window.createImageBitmap=async()=>{window.bitmapAttempts++;throw new DOMException('Cannot decode the data in the argument to createImageBitmap','InvalidStateError')};
   const create=URL.createObjectURL.bind(URL),revoke=URL.revokeObjectURL.bind(URL);
   URL.createObjectURL=blob=>{window.blobCreated++;return create(blob)};
   URL.revokeObjectURL=url=>{window.blobRevoked++;return revoke(url)};
  });
  await page.click('#vr');await page.click('#play');
  await page.waitForFunction(()=>window.blobCreated>=8);
  assert.equal(await page.locator('#state').innerText(),'实时','Image loader recovers from the exact iPhone bitmap error');
  const fallback=await page.evaluate(()=>({attempts:window.bitmapAttempts,liveBlobs:window.blobCreated-window.blobRevoked,pixels:[.25,.75].map(f=>Array.from(ctx.getImageData(canvas.width*f,canvas.height/2,1,1).data))}));
  assert.equal(fallback.attempts,1,'do not retry the incompatible bitmap decoder every frame');
  assert(fallback.liveBlobs<=2,'release old frame Blob URLs');
  assert(fallback.pixels.every(p=>p[0]>150&&p[1]<100),'both eyes continue rendering through native image decoding');
  assert.equal(requests.length,3,'fallback uses the existing stream');
  await page.click('#play');await page.waitForFunction(()=>document.getElementById('play').textContent==='开始观看');
  corrupt=true;
  const report=page.waitForResponse(r=>r.url().endsWith('/api/client-error'));
  await page.click('#play');await report;
  assert.equal(await page.locator('#state').innerText(),'连接中断');
  assert.equal(reports.length,1);assert.equal(reports[0].stage,'decode');assert.match(reports[0].message,/摄像头 JPEG 数据损坏/);
  assert.equal(await page.locator('#ssid').count(),0);assert.deepEqual(errors,[]);
  console.log(`PASS: ${useWebKit?'WebKit':'Chromium'} real HTTP stream, mobile layout, native fullscreen/exit, missing/denied fullscreen fallback, one stream, VGA/quality, stop/restart, bitmap recovery and corrupt-frame report`);
 } finally {await browser?.close();for(const s of [viewer,video]){s?.closeAllConnections();s?.close()}}
})().catch(e=>{console.error(e);process.exitCode=1});
