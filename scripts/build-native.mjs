#!/usr/bin/env node
// Builds the C++ workspace:
//   1. Configure + build the daemon and shm-smoke via plain CMake.
//   2. Build the N-API reader addon via cmake-js.
//   3. Copy the resulting .node into a stable location for the Electron main process.

import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, copyFileSync, rmSync, readdirSync, statSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const repoRoot = resolve(__dirname, '..');
const nativeDir = join(repoRoot, 'native');
const buildDir = join(nativeDir, 'build');
const readerDir = join(nativeDir, 'reader');
const readerStableOut = join(buildDir, 'reader');

const args = process.argv.slice(2);
const clean = args.includes('--clean');
const config = process.env.CMAKE_CONFIG ?? 'Release';

function run(cmd, argv, opts = {}) {
  console.log(`> ${cmd} ${argv.join(' ')}`);
  const res = spawnSync(cmd, argv, { stdio: 'inherit', shell: process.platform === 'win32', ...opts });
  if (res.status !== 0) {
    console.error(`command failed (exit ${res.status}): ${cmd} ${argv.join(' ')}`);
    process.exit(res.status ?? 1);
  }
}

function findFirst(dir, predicate) {
  if (!existsSync(dir)) return null;
  for (const name of readdirSync(dir)) {
    const full = join(dir, name);
    const s = statSync(full);
    if (s.isDirectory()) {
      const found = findFirst(full, predicate);
      if (found) return found;
    } else if (predicate(name, full)) {
      return full;
    }
  }
  return null;
}

if (clean) {
  console.log(`cleaning ${buildDir}`);
  rmSync(buildDir, { recursive: true, force: true });
  rmSync(join(readerDir, 'build'), { recursive: true, force: true });
}

mkdirSync(buildDir, { recursive: true });

// 1. Daemon + shm-smoke.
run('cmake', ['-S', nativeDir, '-B', buildDir, `-DCMAKE_BUILD_TYPE=${config}`]);
run('cmake', ['--build', buildDir, '--config', config, '--parallel']);

// 2. Reader addon via cmake-js.
const cmakeJs = process.platform === 'win32' ? 'cmake-js.cmd' : 'cmake-js';
run(cmakeJs, ['compile', '--directory', readerDir, '--config', config], {
  cwd: repoRoot,
});

// 3. Copy the .node addon to a stable spot the main process can require()
//    without caring about cmake-js's per-config layout.
const builtNode = findFirst(join(readerDir, 'build'), (name) => name === 'shm_reader.node');
if (!builtNode) {
  console.error('build-native: shm_reader.node was not produced under native/reader/build');
  process.exit(1);
}
mkdirSync(readerStableOut, { recursive: true });
const dest = join(readerStableOut, 'shm_reader.node');
copyFileSync(builtNode, dest);
console.log(`build-native: copied addon to ${dest}`);

console.log('build-native: ok');
