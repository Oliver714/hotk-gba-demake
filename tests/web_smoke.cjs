/* Browser integration check; npm install playwright@1.64.1 before running. */
const {chromium, devices} = require('playwright');
const http = require('http');
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const root = path.resolve(process.argv[2] || 'dist/web');
const artifacts = path.resolve(process.env.WEB_TEST_ARTIFACTS || '.cache/web-smoke/results');
fs.mkdirSync(artifacts, {recursive:true});
const mime = {'.html':'text/html; charset=utf-8','.js':'text/javascript','.css':'text/css','.json':'application/json','.png':'image/png','.wasm':'application/wasm'};
const listen = server => new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
const close = server => new Promise(resolve => server.close(resolve));
const game = http.createServer((req,res)=>{
  const relative = decodeURIComponent(new URL(req.url,'http://localhost').pathname).replace(/^\/nested\/game\//,'');
  const file = path.resolve(root, relative || 'index.html');
  if(!file.startsWith(root+path.sep)||!fs.existsSync(file)||!fs.statSync(file).isFile()){res.writeHead(404);res.end();return;}
  res.writeHead(200, {'Content-Type':mime[path.extname(file)]||'application/octet-stream','Content-Length':fs.statSync(file).size});
  if(req.method==='HEAD')res.end();else fs.createReadStream(file).pipe(res);
});
let host, browser;
(async()=>{
  await listen(game);
  const gameOrigin='http://127.0.0.1:'+game.address().port;
  host=http.createServer((req,res)=>{res.setHeader('Content-Type','text/html');res.end(`<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><style>body{margin:0}iframe{border:0;width:100vw;height:100vh}</style><iframe title="game" src="${gameOrigin}/nested/game/index.html" sandbox="allow-scripts allow-same-origin allow-forms allow-pointer-lock allow-downloads" allow="autoplay; fullscreen; gamepad" allowfullscreen></iframe>`);});
  await listen(host);
  const hostOrigin='http://localhost:'+host.address().port;
  const errors=[], external=[];
  browser=await chromium.launch({headless:true,...(process.env.WEB_TEST_CHANNEL?{channel:process.env.WEB_TEST_CHANNEL}:{})});
  const context=await browser.newContext({viewport:{width:800,height:680}});
  const page=await context.newPage();
  page.on('pageerror', e=>errors.push(e.message));
  page.on('console', m=>{if(m.type()==='error')errors.push(m.text());});
  await context.route('**/*', route=>{
    const url=route.request().url();
    if(!url.startsWith(gameOrigin+'/')&&!url.startsWith(hostOrigin+'/')&&!url.startsWith('blob:')&&!url.startsWith('data:')){external.push(url);return route.abort();}
    return route.continue();
  });
  async function start(){
    await page.goto(hostOrigin);
    const frame=page.frames().find(f=>f.url().startsWith(gameOrigin));
    await frame.getByText('开始游戏',{exact:true}).click();
    await frame.waitForFunction(()=>window.EJS_emulator?.started && window.EJS_emulator?.gameManager?.functions?.getFrameNum()>30,{timeout:90000});
    await page.waitForTimeout(2500);
    assert.equal(await frame.evaluate(()=>crossOriginIsolated),false);
    assert.equal(await frame.evaluate(()=>!!EJS_emulator.failedToStart),false);
    return frame;
  }
  const frame=await start();
  await page.screenshot({path:path.join(artifacts,'desktop.png')});
  const press=async key=>{await page.keyboard.down(key);await page.waitForTimeout(130);await page.keyboard.up(key);await page.waitForTimeout(300);};
  // Real keypad navigation: title -> ruler -> confirmation -> domestic menu -> save slot.
  for(let i=0;i<3;++i)await press('x');
  for(let i=0;i<3;++i)await press('ArrowDown');
  await press('x');await press('x');
  await page.waitForTimeout(6500);
  const readSave=()=>{
    const gm=EJS_emulator.gameManager;
    const save=gm.getSaveFile();
    return {size:save?.length,magic:save?Array.from(save.slice(0,4)):[],timer:!!EJS_emulator.saveSaveInterval};
  };
  const saved=await frame.evaluate(readSave);
  assert.equal(saved.size,131072);assert.deepEqual(saved.magic,[0x31,0x51,0x47,0x53]);assert.ok(saved.timer);
  await page.screenshot({path:path.join(artifacts,'save.png')});
  // A fresh iframe on the same browser profile must recover the in-game FLASH save.
  const reloaded=await start();
  const persisted=await reloaded.evaluate(readSave);
  assert.deepEqual(persisted.magic,saved.magic);assert.equal(persisted.size,131072);
  const mobile=await browser.newContext({...devices['Pixel 7'],viewport:{width:390,height:740}});
  const mobilePage=await mobile.newPage();
  await mobilePage.goto(hostOrigin);
  const mobileFrame=mobilePage.frames().find(f=>f.url().startsWith(gameOrigin));
  await mobileFrame.getByText('开始游戏',{exact:true}).click();
  await mobileFrame.waitForFunction(()=>window.EJS_emulator?.started && window.EJS_emulator?.gameManager?.functions?.getFrameNum()>30,{timeout:90000});
  await mobilePage.waitForTimeout(2000);
  assert.ok(await mobileFrame.locator('.ejs_virtualGamepad_button').count()>0);
  await mobileFrame.locator('canvas').tap();
  await mobilePage.waitForTimeout(300);
  await mobilePage.screenshot({path:path.join(artifacts,'mobile.png')});
  assert.equal(await mobileFrame.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  assert.deepEqual(errors,[]);assert.deepEqual(external,[]);
  console.log('PASS: sandbox iframe, offline runtime, keyboard navigation, 128 KiB save, persistence and mobile layout');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{if(browser)await browser.close();if(host)await close(host);await close(game);});
