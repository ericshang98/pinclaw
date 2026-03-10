const ws = new (require('ws'))('ws://127.0.0.1:18789', {
  headers: { 'Origin': 'http://localhost:3100' }
});

ws.on('open', () => {
  console.log('1. WS OPEN');
});

let authed = false;

ws.on('message', (data) => {
  const msg = JSON.parse(data.toString());

  if (msg.type === 'event' && msg.event === 'connect.challenge') {
    console.log('2. Got challenge, sending connect (backend mode)...');
    ws.send(JSON.stringify({
      type: 'req',
      id: 'test-backend',
      method: 'connect',
      params: {
        minProtocol: 3,
        maxProtocol: 3,
        client: {
          id: 'openclaw-managed',
          version: '0.1.0',
          platform: 'web',
          mode: 'webchat',
          instanceId: 'pinclaw-dashboard-1'
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

  if (msg.type === 'res' && !authed) {
    if (msg.ok === false) {
      console.log('3. FAILED:', msg.error?.message);
      ws.close();
    } else {
      authed = true;
      console.log('3. AUTH SUCCESS!');
      console.log('   Sending chat.send...');
      ws.send(JSON.stringify({
        type: 'req',
        id: 'chat-1',
        method: 'chat.send',
        params: {
          sessionKey: 'agent:main:main',
          message: '1+1=? 用一个数字回答',
          idempotencyKey: 'test-' + Date.now()
        }
      }));
    }
    return;
  }

  if (msg.type === 'res' && authed) {
    console.log('4. chat.send ack:', msg.ok === false ? 'FAIL ' + msg.error?.message : 'OK');
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
      console.log('\n6. FINAL:', text.slice(0, 300));
      ws.close();
    } else {
      console.log('\n6. Chat status:', p.state, p.errorMessage || '');
      ws.close();
    }
    return;
  }

  console.log('OTHER:', msg.type, msg.event, JSON.stringify(msg).slice(0, 200));
});

ws.on('error', (err) => { console.log('ERR:', err.message); });
ws.on('close', (code, reason) => {
  console.log('CLOSE:', code, reason?.toString());
  process.exit(0);
});

setTimeout(() => { console.log('\nTIMEOUT'); ws.close(); process.exit(0); }, 60000);
