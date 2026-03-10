const crypto = require('crypto');
const WebSocket = require('ws');

const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519');
const pubRaw = publicKey.export({ type: 'spki', format: 'der' }).slice(-32);
const deviceId = crypto.createHash('sha256').update(pubRaw).digest('hex');
const pubB64 = pubRaw.toString('base64url');

const TOKEN = 'pinclaw-dev-token-2026';
const ws = new WebSocket('ws://192.168.5.29:18789/gateway');

ws.on('open', () => console.log('1. WS OPEN'));
ws.on('message', d => {
  const msg = JSON.parse(d.toString());
  if (msg.event === 'connect.challenge') {
    const nonce = msg.payload.nonce;
    console.log('2. Got challenge, nonce=' + nonce.substring(0, 8) + '...');

    const signedAt = Date.now();
    const payloadStr = `v2|${deviceId}|cli|cli|operator|operator.admin,operator.write,operator.read|${signedAt}|${TOKEN}|${nonce}`;
    const sig = crypto.sign(null, Buffer.from(payloadStr), privateKey).toString('base64url');

    ws.send(JSON.stringify({
      type: 'req', id: 'connect-1', method: 'connect',
      params: {
        minProtocol: 3, maxProtocol: 3,
        client: {id:'cli', mode:'cli', version:'1.0.0', platform:'ios', instanceId: crypto.randomUUID()},
        role: 'operator',
        scopes: ['operator.admin', 'operator.write', 'operator.read'],
        caps: [],
        device: {id: deviceId, publicKey: pubB64, signature: sig, signedAt: signedAt, nonce: nonce},
        auth: {token: TOKEN},
        userAgent: 'pinclaw-ios/1.0.0', locale: 'zh'
      }
    }));
  } else if (msg.type === 'res' && msg.ok === true && msg.id === 'connect-1') {
    const methods = msg.payload?.features?.methods?.length || 0;
    console.log('3. CONNECTED! methods=' + methods);
    ws.send(JSON.stringify({type:'req', id:'rpc-1', method:'sessions.list', params:{}}));
  } else if (msg.type === 'res' && msg.id === 'rpc-1') {
    const sessions = msg.payload?.sessions || [];
    console.log('4. sessions.list OK! count=' + sessions.length);
    console.log('\n=== LOCAL GATEWAY: FULL SUCCESS ===');
    ws.close();
  } else if (msg.type === 'res' && msg.ok === false) {
    console.log('ERROR:', JSON.stringify(msg.error));
    ws.close();
  }
});
ws.on('close', (c,r) => console.log('CLOSE:', c, r?.toString()));
ws.on('error', e => console.log('ERR:', e.message));
setTimeout(() => process.exit(0), 8000);
