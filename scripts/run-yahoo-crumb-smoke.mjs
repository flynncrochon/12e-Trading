#!/usr/bin/env node
// Convenience wrapper around the yahoo-crumb-smoke binary. Runs the
// fc.yahoo.com → getcrumb handshake once and prints the crumb on success.

import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const repoRoot = resolve(__dirname, '..');

const binary = process.platform === 'win32' ? 'yahoo-crumb-smoke.exe' : 'yahoo-crumb-smoke';
const candidates = [
  join(repoRoot, 'native', 'build', 'tools', 'Release', binary),
  join(repoRoot, 'native', 'build', 'tools', 'Debug', binary),
  join(repoRoot, 'native', 'build', 'tools', binary),
];

const exe = candidates.find(existsSync);
if (!exe) {
  console.error('yahoo-crumb-smoke binary not found. Did you run `pnpm build:native`?');
  console.error('Looked in:');
  for (const c of candidates) console.error(`  ${c}`);
  process.exit(1);
}

console.log(`running ${exe}`);
const res = spawnSync(exe, [], { stdio: 'inherit' });
process.exit(res.status ?? 0);
