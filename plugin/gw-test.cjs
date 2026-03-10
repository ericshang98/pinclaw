const ws = new (require('ws'))('ws://127.0.0.1:18789', {
  headers: { 'Origin': 'http://localhost:3100' }
});

ws.on('open', () => {
  console.log('1. WS OPEN');
});

ws.on('message', (data) => {
  const msg = JSON.parse(data.toString());

  if (msg.type === 'event' && msg.event === 'connect.challenge') {
    console.log('2. Got challenge, sending connect...');
    ws.send(JSON.stringify({
      type: 'req',
      id: 'test-final',
      method: 'connect',
      params: {
        minProtocol: 3,
        maxProtocol: 3,
        client: {
          id: 'openclaw-control-ui',
          version: '0.1.0',
          platform: 'web',
          mode: 'webchat',
          instanceId: 'pinclaw-test-1'
        },
        role: 'operator',
        scopes: ['operator.admin','operator.write','operator.read'],
        caps: [],
        auth: { token: 'dbc36817d73d52ebf7468ae9fb4655ca' },
        userAgent: 'pinclaw-web/0.1.0',
        locale: 'zh'
      }
    }));
    return;
  }

  if (msg.type === 'res') {
    if (msg.ok === false) {
      console.log('3. AUTH FAILED:', JSON.stringify(msg.error));
      ws.close();
    } else {
      console.log('3. AUTH SUCCESS! Connected to Gateway');
      console.log('   Scopes:', JSON.stringify(msg.payload?.auth?.scopes));

      console.log('4. Sending chat.send...');
      ws.send(JSON.stringify({
        type: 'req',
        id: 'chat-1',
        method: 'chat.send',
        params: {
          sessionKey: 'agent:main:main',
          message: '你好，请用一句话回答：1+1等于几？',
          idempotencyKey: 'test-' + Date.now()
        }
      }));
    }
    return;
  }

  if (msg.type === 'event' && msg.event === 'chat') {
    const p = msg.payload;
    if (p.state === 'delta') {
      const text = typeof p.message?.content === 'string'
        ? p.message.content
        : Array.isArray(p.message?.content)
          ? p.message.content.filter(b => b.type === 'text').map(b => b.text).join('')
          : '';
      process.stdout.write('\r5. Streaming: ' + text.slice(0, 120));
    } else if (p.state === 'final') {
      const text = typeof p.message?.content === 'string'
        ? p.message.content
        : Array.isArray(p.message?.content)
          ? p.message.content.filter(b => b.type === 'text').map(b => b.text).join('')
          : '';
      console.log('\n6. FINAL RESPONSE:', text.slice(0, 300));
      ws.close();
    } else if (p.state === 'error' || p.state === 'aborted') {
      console.log('\n6. Chat error:', p.errorMessage || p.state);
      ws.close();
    }
    return;
  }

  console.log('OTHER:', msg.type, msg.event || '', JSON.stringify(msg).slice(0, 200));
});

ws.on('error', (err) => { console.log('ERR:', err.message); });
ws.on('close', (code, reason) => {
  console.log('CLOSE:', code, reason?.toString());
  process.exit(0);
});

setTimeout(() => { console.log('\nTIMEOUT (60s)'); ws.close(); process.exit(0); }, 60000);
