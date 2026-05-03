import { app } from 'electron';
import { existsSync } from 'node:fs';
import { join, resolve } from 'node:path';

const DAEMON_BINARY = process.platform === 'win32' ? 'market-data-service.exe' : 'market-data-service';
const READER_ADDON = 'shm_reader.node';

/**
 * Resolve the path to the C++ market-data-service binary.
 *
 * In dev: native/build/daemon/<config>/market-data-service[.exe]
 * Packaged: process.resourcesPath/market-data-service[.exe]
 *
 * Centralised so the dev/packaged split has exactly one source of truth.
 */
export function resolve_daemon_path(): string {
  if (app.isPackaged) {
    return join(process.resourcesPath, DAEMON_BINARY);
  }

  const build_root = resolve(__dirname, '..', '..', 'native', 'build', 'daemon');
  const candidates = [
    join(build_root, 'Release', DAEMON_BINARY),
    join(build_root, 'Debug', DAEMON_BINARY),
    join(build_root, 'RelWithDebInfo', DAEMON_BINARY),
    join(build_root, DAEMON_BINARY),
  ];
  for (const p of candidates) {
    if (existsSync(p)) return p;
  }
  return candidates[0];
}

/**
 * Resolve the path to the shm_reader.node N-API addon.
 *
 * The build script copies the artifact to `native/build/reader/shm_reader.node`
 * regardless of cmake-js's per-config subdir, so we only have to look in one place.
 */
export function resolve_reader_addon_path(): string {
  if (app.isPackaged) {
    // asarUnpack ensures *.node is on disk under app.asar.unpacked/
    return join(process.resourcesPath, 'app.asar.unpacked', 'native', 'build', 'reader', READER_ADDON);
  }
  return resolve(__dirname, '..', '..', 'native', 'build', 'reader', READER_ADDON);
}
