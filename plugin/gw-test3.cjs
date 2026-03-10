const clientId = process.argv[2] || 'openclaw-app';
const ws = new (require('ws'))('ws://127.0.0.1:18789', {
  headers: { 'Origin': 'http://localhost:3100' }
});
ws.on('open', () => console.log('OPEN'));
ws.on('message', (data) => {
  const msg = JSON.parse(data.toString());
  if (msg.type === 'event' && msg.event === 'connect.challenge') {
    ws.send(JSON.stringify({
      type: 'req', id: 'test', method: 'connect',
      params: {
        minProtocol: 3, maxProtocol: 3,
        client: { id: clientId, version: '0.1.0', platform: 'web', mode: 'webchat', instanceId: 'test' },
        role: 'operator',
        scopes: ['operator.admin','operator.write','operator.read'],
        caps: [],
        auth: { token: 'dbc36817d73d52ebf7468ae9fb4655ca' },
        userAgent: 'test/1.0', locale: 'en'
      }
    }));
  } else if (msg.type === 'res') {
    console.log(clientId + ':', msg.ok === false ? 'FAIL: ' + msg.error?.message : 'SUCCESS');
    ws.close();
  }
});
ws.on('close', () => process.exit(0));
setTimeout(() => process.exit(0), 5000);
