// CursorPad stdio boundary around the genuine upstream filesystem server.
// Write tools are unavailable even to a client sending raw MCP requests.
import { Transform } from 'node:stream';
import { StringDecoder } from 'node:string_decoder';
import { realpathSync } from 'node:fs';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { createServer } from './filesystem/server.js';

const allowed = new Set(['list_directory', 'get_file_info', 'read_text_file']);
const root = realpathSync(process.argv[2]);
let pending = '';
const inputDecoder = new StringDecoder('utf8'), outputDecoder = new StringDecoder('utf8');
const input = new Transform({transform(chunk, _encoding, done) {
  pending += inputDecoder.write(chunk);
  if (Buffer.byteLength(pending) > 65536) return done(new Error('MCP request too large'));
  let end;
  while ((end = pending.indexOf('\n')) >= 0) {
    const line = pending.slice(0, end); pending = pending.slice(end + 1);
    try {
      const request = JSON.parse(line);
      const permitted = request.method === 'initialize' || request.method === 'notifications/initialized' ||
        request.method === 'tools/list' || (request.method === 'tools/call' && allowed.has(request.params?.name));
      if (permitted) this.push(line + '\n');
      else if (request.id !== undefined) process.stdout.write(JSON.stringify({jsonrpc:'2.0', id:request.id,
        error:{code:-32601, message:'CursorPad exposes read-only filesystem tools; mutations require the native plan.'}}) + '\n');
    } catch { return done(new Error('Invalid MCP request')); }
  }
  done();
}});
// Do not advertise tools which the boundary refuses.
let outputPending = '';
const output = new Transform({transform(chunk, _encoding, done) {
  outputPending += outputDecoder.write(chunk);
  let end;
  while ((end = outputPending.indexOf('\n')) >= 0) {
    const line = outputPending.slice(0, end); outputPending = outputPending.slice(end + 1);
    try {
      const response = JSON.parse(line);
      if (Array.isArray(response.result?.tools)) response.result.tools = response.result.tools.filter(t => allowed.has(t.name));
      this.push(JSON.stringify(response) + '\n');
    } catch { return done(new Error('Invalid MCP response')); }
  }
  done();
}});
process.stdin.pipe(input); output.pipe(process.stdout);
createServer([root]).connect(new StdioServerTransport(input, output)).catch(error => {
  console.error(String(error)); process.exitCode = 1;
});
