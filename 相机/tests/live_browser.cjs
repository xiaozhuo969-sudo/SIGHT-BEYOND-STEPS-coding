// Real device check. Pass its LAN URL; stop other viewers before running.
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
 const url=process.argv[2];if(!url)throw Error('Usage: node tests/live_browser.cjs http://DEVICE-IP/');
 const browser=await chromium.launch({executablePath:process.env.CHROME_PATH||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox','--no-proxy-server']});
 try {
  const page=await browser.newPage();const failures=[];let corsCount=0,streamType='',streamFormat='';
  page.on('requestfailed',r=>failures.push({url:r.url(),error:r.failure()?.errorText}));
  page.on('console',m=>{if(m.type()==='error')console.log('browser:',m.text())});
  page.on('response',async r=>{const u=new URL(r.url());if(u.pathname==='/stream'){corsCount=(await r.headersArray()).filter(h=>h.name.toLowerCase()==='access-control-allow-origin').length;streamType=r.headers()['content-type'];streamFormat=u.searchParams.get('format')}});
  await page.addInitScript(()=>{window.decodedFrames=0});
  await page.goto(url);
  await page.evaluate(()=>{const decode=window.decode;window.decode=async(...args)=>{const frame=await decode(...args);window.decodedFrames++;return frame}});
  await page.waitForFunction(()=>document.getElementById('state').textContent==='实时',{},{timeout:15000});
  await page.waitForFunction(()=>document.getElementById('stats').textContent.includes('fps'),{},{timeout:10000});
  assert.equal(corsCount,1,'exactly one CORS allow-origin header');
  assert.equal(streamType,'application/octet-stream');assert.equal(streamFormat,'bytes');
  const begin=await page.evaluate(()=>({frames:window.decodedFrames,time:performance.now()}));
  await page.waitForTimeout(30000);
  const end=await page.evaluate(()=>({frames:window.decodedFrames,time:performance.now()}));
  assert.equal(await page.locator('#state').innerText(),'实时');
  const stats={average_display_fps:((end.frames-begin.frames)*1000/(end.time-begin.time)).toFixed(2),last:await page.locator('#stats').innerText()};
  await page.click('#vr');assert.equal(await page.locator('#state').innerText(),'实时');
  await page.click('#play');await page.waitForFunction(()=>document.getElementById('play').textContent==='开始观看');
  await page.waitForTimeout(1500);
  await page.selectOption('#size','vga');await page.click('#play');
  await page.waitForFunction(()=>document.getElementById('state').textContent==='实时',{},{timeout:15000});
  await page.waitForFunction(()=>document.getElementById('stats').textContent.includes('640 × 480'),{},{timeout:15000});
  await page.click('#play');await page.waitForFunction(()=>document.getElementById('play').textContent==='开始观看');
  await page.waitForTimeout(1500);
  await page.selectOption('#size','qvga');await page.selectOption('#quality','fine');await page.click('#play');
  await page.waitForFunction(()=>document.getElementById('state').textContent==='实时',{},{timeout:15000});
  await page.waitForFunction(()=>document.getElementById('stats').textContent.includes('320 × 240'),{},{timeout:15000});
  await page.click('#play');await page.waitForFunction(()=>document.getElementById('play').textContent==='开始观看');
  console.log('PASS: 30s playback, VGA and back, quality switch, live device stream decoded, one CORS header, VR toggle and stop;',stats);
 } catch(e) {throw e} finally {await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
