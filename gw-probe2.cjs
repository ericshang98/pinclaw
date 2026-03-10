const WebSocket = require('ws');
const fs = require('fs');
const cfg = JSON.parse(fs.readFileSync(require('os').homedir()+'/.openclaw/openclaw.json','utf8'));
const token = cfg.gateway?.auth?.token;
const ws = new WebSocket('ws://127.0.0.1:18789');

ws.on('open', () => {
  ws.send(JSON.stringify({type:'req',id:'c1',method:'connect',params:{
    minProtocol:3,maxProtocol:3,
    client:{id:'probe',version:'0.1',platform:'node',mode:'dashboard'},
    role:'operator',scopes:['operator.admin','operator.write','operator.read'],
    caps:['dashboard'],auth:{token},userAgent:'probe'
  }}));
});

let ready = false;
const allMessages = [];

ws.on('message', (raw) => {
  const msg = JSON.parse(raw.toString());
  allMessages.push(msg);

  if (msg.type === 'event' && msg.event === 'connect.challenge') {
    ws.send(JSON.stringify({type:'req',id:'c1',method:'connect',params:{
      minProtocol:3,maxProtocol:3,
      client:{id:'probe',version:'0.1',platform:'node',mode:'dashboard'},
      role:'operator',scopes:['operator.admin'],caps:['dashboard'],
      auth:{token, nonce:msg.payload?.nonce},userAgent:'probe'
    }}));
    return;
  }

  if (msg.type === 'res' && msg.id === 'c1') {
    ready = true;
    console.log('=== Connect Response ===');
    const p = msg.payload || {};
    console.log('Features methods:', JSON.stringify(p.features?.methods || []));
    console.log('Features events:', JSON.stringify(p.features?.events || []));
    console.log('Full payload keys:', Object.keys(p));
    console.log(JSON.stringify(p, null, 2).slice(0, 3000));

    // Wait 2s then dump everything received
    setTimeout(() => {
      console.log('\n=== All messages received ===');
      allMessages.forEach((m, i) => {
        if (m.type === 'event') {
          console.log(`[${i}] event: ${m.event}`, JSON.stringify(m.payload || {}).slice(0, 200));
        } else {
          console.log(`[${i}] ${m.type}: id=${m.id}`, JSON.stringify(m).slice(0, 200));
        }
      });
      ws.close();
      process.exit(0);
    }, 2000);
    return;
  }

  // Log all events
  if (msg.type === 'event') {
    console.log('EVENT:', msg.event, JSON.stringify(msg.payload || {}).slice(0, 300));
  }
});

ws.on('error', (e) => { console.error('Error:', e.message); process.exit(1); });
setTimeout(() => { ws.close(); process.exit(0); }, 10000);
