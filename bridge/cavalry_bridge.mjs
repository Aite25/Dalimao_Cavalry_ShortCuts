// cavalry_bridge.mjs 鈥?a tiny MCP-over-SSE bridge to a running Cavalry instance.
//
// Cavalry ships an MCP server (Preferences 鈫?MCP Server, default port 6768).
// It speaks the legacy HTTP+SSE MCP transport:
//   GET  /sse                      -> text/event-stream; first event is "endpoint"
//   POST <endpoint>?session_id=... -> JSON-RPC, responses arrive on the SSE stream
// Every new SSE session triggers a GUI dialog in Cavalry ("<name> wants to connect鈥?),
// so we keep ONE session alive and expose a local HTTP shim for tool calls.
//
// Local API (127.0.0.1:6769):
//   GET  /status            -> { connected, initialized, tools, pendingAuth }
//   GET  /tools             -> tools/list result
//   POST /call {name,args}  -> tools/call result (MCP content array)
//   GET  /raw?method=...    -> any JSON-RPC method, returns result

import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const CAVALRY_PORT = Number(process.env.CAVALRY_MCP_PORT || 6768);
const LOCAL_PORT = Number(process.env.BRIDGE_PORT || 6769);

// Cavalry only accepts a 64-hex-character (256-bit) fingerprint; anything else
// is refused. Same convention as the official bridge: one persistent secret per
// client name in ~/.cavalry-mcp/fingerprint-<slug>, so Cavalry asks "allow this
// client?" once and remembers it afterwards.
function fingerprintFor(name) {
  const slug = ((name || 'default').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '') || 'default').slice(0, 40);
  const dir = path.join(os.homedir(), '.cavalry-mcp');
  const file = path.join(dir, 'fingerprint-' + slug);
  let token = '';
  try { token = fs.readFileSync(file, 'utf8').trim(); } catch {}
  if (!/^[0-9a-f]{64}$/.test(token)) {
    token = crypto.randomBytes(32).toString('hex');
    fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
    fs.writeFileSync(file, token, { mode: 0o600 });
    log(`created new fingerprint ${file}`);
  }
  return token;
}
const CLIENT_NAME = process.env.CAVALRY_MCP_CLIENT_NAME || 'DSH (DeepSeek Harness)';
const CLIENT_VERSION = '0.1.0';
// 日志写在脚本旁边，别用绝对路径（换机器就废了）
const HERE = path.dirname(fileURLToPath(import.meta.url));
const LOG = process.env.BRIDGE_LOG || path.join(HERE, 'cavalry_bridge.log');

const state = {
  connected: false,
  initialized: false,
  pendingAuth: true,
  endpoint: null,
  tools: null,
  lastError: null,
  protocolVersion: null,
  serverInfo: null,
  startedAt: new Date().toISOString(),
};

function log(...args) {
  const line = `[${new Date().toISOString()}] ${args.map(a => typeof a === 'string' ? a : JSON.stringify(a)).join(' ')}`;
  console.log(line);
  try { fs.appendFileSync(LOG, line + '\n'); } catch {}
}

let nextId = 1;
const pending = new Map();

const FINGERPRINT = process.env.CAVALRY_MCP_FINGERPRINT || fingerprintFor(CLIENT_NAME);

function sendJson(obj) {
  const body = JSON.stringify(obj);
  const u = new URL(state.endpoint);
  const req = http.request({
    hostname: u.hostname === '[::1]' || u.hostname === '::1' ? '::1' : u.hostname,
    port: u.port || CAVALRY_PORT,
    path: u.pathname + u.search,
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Accept': 'application/json, text/event-stream',
      'Content-Length': Buffer.byteLength(body),
      'X-Cavalry-MCP-Fingerprint': FINGERPRINT,
      'X-Cavalry-MCP-Client-Name': CLIENT_NAME,
      'X-Cavalry-MCP-Client-Version': CLIENT_VERSION,
    },
  }, res => {
    let data = '';
    res.on('data', c => data += c);
    res.on('end', () => {
      if (res.statusCode !== 200 && res.statusCode !== 202 && res.statusCode !== 204) {
        log(`POST ${u.pathname} -> ${res.statusCode} ${data.slice(0, 300)}`);
      }
    });
  });
  req.on('error', e => log('POST error:', e.message));
  req.write(body);
  req.end();
}

function rpc(method, params, timeoutMs = 120000) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`timeout waiting for ${method} (${timeoutMs}ms)`));
    }, timeoutMs);
    pending.set(id, { resolve, reject, timer, method });
    sendJson({ jsonrpc: '2.0', id, method, params: params ?? {} });
  });
}

function notify(method, params) {
  sendJson({ jsonrpc: '2.0', method, params: params ?? {} });
}

// ---------------------------------------------------------------- SSE connect
// fetch() + a streaming reader: Node's plain http client received the 200 and
// the endpoint event's bytes but never surfaced them, so use undici instead.
async function connect() {
  log(`connecting to http://[::1]:${CAVALRY_PORT}/sse as "${CLIENT_NAME}"`);
  try {
    const res = await fetch(`http://[::1]:${CAVALRY_PORT}/sse`, {
      method: 'GET',
      headers: {
        'Accept': 'text/event-stream',
        'Cache-Control': 'no-cache',
        'X-Cavalry-MCP-Fingerprint': FINGERPRINT,
        'X-Cavalry-MCP-Client-Name': CLIENT_NAME,
        'X-Cavalry-MCP-Client-Version': CLIENT_VERSION,
      },
    });
    log(`SSE status ${res.status}`);
    if (res.status !== 200) {
      state.pendingAuth = false;
      state.lastError = `SSE HTTP ${res.status}`;
      setTimeout(connect, 3000);
      return;
    }
    state.connected = true;
    state.pendingAuth = false;
    state.lastError = null;
    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buf = '';
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += decoder.decode(value, { stream: true }).replace(/\r\n/g, '\n');
      let idx;
      while ((idx = buf.indexOf('\n\n')) !== -1) {
        const raw = buf.slice(0, idx);
        buf = buf.slice(idx + 2);
        handleEvent(raw);
      }
    }
    log('SSE stream ended');
  } catch (e) {
    log('SSE error:', e.message);
    state.lastError = e.message;
  }
  state.connected = false;
  state.initialized = false;
  state.pendingAuth = true;
  setTimeout(connect, 2000);
}

function handleEvent(raw) {
  const lines = raw.split('\n');
  let event = 'message';
  const dataLines = [];
  for (const line of lines) {
    if (line.startsWith('event:')) event = line.slice(6).trim();
    else if (line.startsWith('data:')) dataLines.push(line.slice(5).replace(/^ /, ''));
  }
  const data = dataLines.join('\n');
  if (event === 'endpoint') {
    state.endpoint = data.startsWith('http') ? data : `http://localhost:${CAVALRY_PORT}${data.startsWith('/') ? '' : '/'}${data}`;
    log('endpoint event ->', state.endpoint);
    handshake();
    return;
  }
  if (!data) return;
  let msg;
  try { msg = JSON.parse(data); } catch { log('bad SSE data', data.slice(0, 200)); return; }
  if (msg.id !== undefined && pending.has(msg.id)) {
    const p = pending.get(msg.id);
    pending.delete(msg.id);
    clearTimeout(p.timer);
    if (msg.error) p.reject(new Error(`${p.method}: ${JSON.stringify(msg.error)}`));
    else p.resolve(msg.result);
  } else if (msg.method) {
    log('notification from server:', msg.method);
  }
}

async function handshake() {
  try {
    const init = await rpc('initialize', {
      protocolVersion: '2025-11-25',
      capabilities: {},
      clientInfo: { name: CLIENT_NAME, version: CLIENT_VERSION },
    }, 300000); // long: the user may need a moment to click Allow in Cavalry
    state.protocolVersion = init?.protocolVersion ?? null;
    state.serverInfo = init?.serverInfo ?? null;
    log('initialize ok:', init);
    notify('notifications/initialized');
    state.initialized = true;
    const list = await rpc('tools/list', {}, 30000);
    state.tools = (list?.tools ?? []).map(t => t.name);
    log(`tools (${state.tools.length}):`, state.tools.join(', '));
  } catch (e) {
    state.lastError = e.message;
    log('handshake failed:', e.message);
  }
}

// ---------------------------------------------------------------- local shim
const server = http.createServer((req, res) => {
  const u = new URL(req.url, `http://127.0.0.1:${LOCAL_PORT}`);
  const json = (code, obj) => {
    const body = JSON.stringify(obj);
    res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': Buffer.byteLength(body) });
    res.end(body);
  };
  if (u.pathname === '/status') {
    return json(200, {
      connected: state.connected,
      initialized: state.initialized,
      pendingAuth: state.pendingAuth,
      toolCount: state.tools?.length ?? 0,
      tools: state.tools,
      protocolVersion: state.protocolVersion,
      serverInfo: state.serverInfo,
      lastError: state.lastError,
      endpoint: state.endpoint,
      startedAt: state.startedAt,
    });
  }
  if (u.pathname === '/tools') {
    if (!state.initialized) return json(503, { error: 'not initialized', detail: state.lastError });
    return json(200, { tools: state.tools });
  }
  if (u.pathname === '/tool-defs') {
    if (!state.initialized) return json(503, { error: 'not initialized', detail: state.lastError });
    rpc('tools/list', {}, 30000).then(r => json(200, r)).catch(e => json(500, { error: e.message }));
    return;
  }
  if (u.pathname === '/call' && req.method === 'POST') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', async () => {
      let payload;
      try { payload = JSON.parse(body || '{}'); } catch { return json(400, { error: 'bad json' }); }
      if (!state.initialized) return json(503, { error: 'not initialized', detail: state.lastError });
      try {
        const r = await rpc('tools/call', { name: payload.name, arguments: payload.args ?? {} }, payload.timeoutMs ?? 300000);
        json(200, r);
      } catch (e) {
        json(500, { error: e.message });
      }
    });
    return;
  }
  if (u.pathname === '/raw') {
    const method = u.searchParams.get('method');
    if (!method) return json(400, { error: 'method required' });
    let params = {};
    if (u.searchParams.get('params')) {
      try { params = JSON.parse(u.searchParams.get('params')); } catch { return json(400, { error: 'bad params json' }); }
    }
    rpc(method, params, 120000).then(r => json(200, r)).catch(e => json(500, { error: e.message }));
    return;
  }
  json(404, { error: 'not found', routes: ['/status', '/tools', '/tool-defs', '/call', '/raw'] });
});

server.listen(LOCAL_PORT, '127.0.0.1', () => {
  log(`bridge listening on http://127.0.0.1:${LOCAL_PORT}`);
  connect();
});

process.on('SIGINT', () => { log('bye'); process.exit(0); });
