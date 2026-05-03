#!/usr/bin/env node
// Convenience wrapper around the yahoo-quote-smoke binary. Forwards any
// extra args as the symbol list (default symbols if none given).

import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const repoRoot = resolve(__dirname, '..');

const binary = process.platform === 'win32' ? 'yahoo-quote-smoke.exe' : 'yahoo-quote-smoke';
const candidates = [
  join(repoRoot, 'native', 'build', 'tools', 'Release', binary),
  join(repoRoot, 'native', 'build', 'tools', 'Debug', binary),
  join(repoRoot, 'native', 'build', 'tools', binary),
];

const exe = candidates.find(existsSync);
if (!exe) {
  console.error('yahoo-quote-smoke binary not found. Did you run `pnpm build:native`?');
  console.error('Looked in:');
  for (const c of candidates) console.error(`  ${c}`);
  process.exit(1);
}

const passthrough = process.argv.slice(2);
console.log(`running ${exe} ${passthrough.join(' ')}`.trim());
const res = spawnSync(exe, passthrough, { stdio: 'inherit' });
process.exit(res.status ?? 0);
