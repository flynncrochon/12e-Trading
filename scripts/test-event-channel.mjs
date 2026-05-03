#!/usr/bin/env node
// Standalone verifier for the daemon's --event-port channel. Listens on an
// ephemeral port, spawns the daemon with --event-port=N, parses frames as
// they arrive, and prints a one-line summary per frame. Kills the daemon
// after `--seconds N` (default 60) so the script doesn't run forever.
//
//   $ node scripts/test-event-channel.mjs
//   listening on port=54321
//   [history:backfill] symbol_id=0 points=1951
//   [summary:update] symbol_id=0 month_ago_price=265.40
//   ...

import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import * as net from 'node:net';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const repoRoot = resolve(__dirname, '..');

const argv = process.argv.slice(2);
const sec_idx = argv.indexOf('--seconds');
const max_seconds = sec_idx >= 0 ? Number(argv[sec_idx + 1]) : 60;

const binary = process.platform === 'win32' ? 'market-data-service.exe' : 'market-data-service';
const candidates = [
  join(repoRoot, 'native', 'build', 'daemon', 'Release', binary),
  join(repoRoot, 'native', 'build', 'daemon', 'Debug', binary),
  join(repoRoot, 'native', 'build', 'daemon', binary),
];
const exe = candidates.find(existsSync);
if (!exe) {
  console.error('daemon binary not found. Did you run pnpm build:native?');
  process.exit(1);
}

let daemon = null;
let buffer = Buffer.alloc(0);
let frame_count = 0;

const server = net.createServer((socket) => {
  console.log(`daemon connected from ${socket.remoteAddress}:${socket.remotePort}`);
  socket.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    while (buffer.length >= 4) {
      const len = buffer.readUInt32LE(0);
      if (buffer.length < 4 + len) break;
      const payload = buffer.subarray(4, 4 + len);
      buffer = buffer.subarray(4 + len);
      frame_count += 1;
      try {
        const ev = JSON.parse(payload.toString('utf8'));
        if (ev.type === 'history:backfill') {
          console.log(`[${ev.type}] symbol_id=${ev.symbol_id} points=${ev.points?.length ?? 0}`);
        } else if (ev.type === 'summary:update') {
          console.log(`[${ev.type}] symbol_id=${ev.symbol_id} month_ago_price=${ev.month_ago_price}`);
        } else {
          console.log(`[${ev.type}] (unknown)`, JSON.stringify(ev).slice(0, 120));
        }
      } catch (err) {
        console.error('malformed JSON frame:', err.message);
      }
    }
  });
  socket.on('close', () => console.log('daemon socket closed'));
  socket.on('error', (err) => console.error('socket error:', err.message));
});

server.listen({ host: '127.0.0.1', port: 0 }, () => {
  const { port } = server.address();
  console.log(`listening on port=${port}`);
  daemon = spawn(exe, [`--event-port=${port}`], { stdio: ['ignore', 'inherit', 'inherit'] });
  daemon.on('exit', (code, signal) => {
    console.log(`daemon exited code=${code} signal=${signal}`);
    server.close();
    console.log(`done. frames received: ${frame_count}`);
    process.exit(0);
  });
});

setTimeout(() => {
  console.log(`max_seconds=${max_seconds} elapsed; killing daemon`);
  if (daemon) daemon.kill();
}, max_seconds * 1000);
