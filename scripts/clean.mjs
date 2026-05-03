#!/usr/bin/env node
import { rmSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const repoRoot = resolve(__dirname, '..');

const targets = [
  'out',
  'dist',
  'release',
  'native/build',
  'native/reader/build',
];

for (const t of targets) {
  const p = join(repoRoot, t);
  console.log(`rm -rf ${p}`);
  rmSync(p, { recursive: true, force: true });
}

console.log('clean: ok');
