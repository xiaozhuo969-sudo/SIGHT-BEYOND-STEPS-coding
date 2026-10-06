const {chromium}=require('playwright');
const fs=require('node:fs'),http=require('node:http'),path=require('node:path'),assert=require('node:assert/strict');
(async()=>{
 let browser,server,peer;
 try{
  const html=fs.readFileSync(path.join(__dirname,'../main/web/index.html'),'utf8');
  server=http.createServer((req,res)=>{if(req.url==='/api/status'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify({camera:false,camera_error:'test image unavailable',psram:8388608}));return}res.setHeader('Content-Type','text/html');res.end(html)});
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  browser=await chromium.launch({executablePath:process.env.CHROME_PATH||'/usr/bin/google-chrome',args:['--no-sandbox']});
  const page=await browser.newPage({viewport:{width:900,height:480}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.routeWebSocket('**/ws/view',ws=>{peer=ws});
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.waitForFunction(()=>hud.socket?.readyState===WebSocket.OPEN);
  const packet={connected:true,uplink_age_ms:0,data:{v:1,session:99,seq:1,spo2_x10:980,spo2_age_ms:5000,voltage_mv:3780,voltage_age_ms:500,pitch_cdeg:240,roll_cdeg:-180,distance_cm:126,air_age_ms:20}};
  const send=()=>peer.send(JSON.stringify(packet));
  send();await page.waitForFunction(()=>hudReadings().voltage==='3.78');
  let view=await page.evaluate(()=>hudReadings());assert.equal(view.spo2,'98.0');assert.equal(view.pitch,'+2.4');assert.equal(view.roll,'-1.8');assert.equal(view.distance,'1.26');
  await page.evaluate(()=>{stage.requestFullscreen=undefined;stage.webkitRequestFullscreen=undefined});
  await page.click('#full');await page.waitForFunction(()=>document.body.classList.contains('immersive'));
  assert.match(await page.locator('#view').getAttribute('aria-label'),/3.78 V/,'HUD remains available in iPhone-style page mode');
  packet.data.air_age_ms=1400;send();await page.waitForFunction(()=>hudReadings().pitch==='--');view=await page.evaluate(()=>hudReadings());assert.equal(view.voltage,'3.78');assert.equal(view.spo2,'98.0');assert.match(view.status,/手控已连接.*飞机未回传/);
  packet.connected=false;send();await page.waitForFunction(()=>!hudReadings().link);assert.equal((await page.evaluate(()=>hudReadings())).voltage,'--');
  packet.connected=true;
  packet.data.air_age_ms=0;packet.data.distance_cm=0;packet.data.pitch_cdeg=0;packet.data.roll_cdeg=0;packet.data.spo2_x10=null;packet.data.spo2_age_ms=null;send();
  await page.waitForFunction(()=>hudReadings().distance==='0.00');view=await page.evaluate(()=>hudReadings());assert.equal(view.pitch,'+0.0');assert.equal(view.spo2,'--');
  packet.data.voltage_mv=5030;send();await page.waitForFunction(()=>hudReadings().voltage==='5.03');
  await page.waitForFunction(()=>hudReadings().voltage==='--'); // No video frames: independent timer still expires data.
  await page.waitForFunction(()=>document.getElementById('telemetry-status').textContent.includes('中断'));
  assert.match(await page.locator('#telemetry-status').innerText(),/中断/);
  packet.data.voltage_age_ms=6000;send();await page.waitForFunction(()=>hudReadings().link);assert.equal((await page.evaluate(()=>hudReadings())).voltage,'--');
  packet.data.voltage_age_ms=0;packet.data.spo2_x10=980;packet.data.spo2_age_ms=11000;send();await page.waitForFunction(()=>hudReadings().voltage==='5.03');assert.equal((await page.evaluate(()=>hudReadings())).spo2,'--');
  await page.click('#exit-view');await page.waitForFunction(()=>!document.body.classList.contains('immersive'));
  await page.click('#vr');assert.match(await page.locator('#vr').innerText(),/关/);assert.match(await page.locator('#view').getAttribute('aria-label'),/5.03 V/);
  const old=peer;old.close();await page.waitForFunction(()=>hud.socket?.readyState!==WebSocket.OPEN);assert.equal((await page.evaluate(()=>hudReadings())).pitch,'--');
  await page.waitForFunction(()=>hud.socket?.readyState===WebSocket.OPEN);assert.notEqual(peer,old);send();await page.waitForFunction(()=>hudReadings().voltage==='5.03');
  peer.send('{"connected":true,"data":{"v":1}}');await page.waitForFunction(()=>hud.packet===null);
  assert.deepEqual(errors,[]);
  console.log('PASS: real page HUD over mocked WebSocket; voltage incl. USB supply, aircraft units, null/zero, independent aging without video, reconnect, malformed data, single-eye');
 }finally{await browser?.close();server?.closeAllConnections();server?.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
