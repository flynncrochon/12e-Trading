import { type ChildProcess, spawn } from 'node:child_process';
import { dirname } from 'node:path';
import { logger } from './logger';

const MAX_CRASH_RESTARTS = 3;
const RESTART_BACKOFF_MS = 1000;
const STOP_GRACE_MS = 2000;

/**
 * Spawns and supervises the C++ market-data-service. Restarts on crash up to
 * a small budget; gives the process a graceful-stop window before SIGKILL/taskkill.
 */
export class DaemonSupervisor {
  private child: ChildProcess | null = null;
  private stopping = false;
  private crashes = 0;

  constructor(private readonly binary_path: string, private readonly args: string[] = []) {}

  start(): void {
    if (this.child) return;
    this.stopping = false;
    this.spawn_child();
  }

  private spawn_child(): void {
    logger.info({ binary: this.binary_path, args: this.args }, 'daemon-supervisor: spawning');
    const child = spawn(this.binary_path, this.args, {
      cwd: dirname(this.binary_path),
      stdio: ['ignore', 'pipe', 'pipe'],
      windowsHide: true,
    });

    child.stdout?.on('data', (chunk: Buffer) => {
      process.stdout.write(`[daemon] ${chunk.toString('utf8')}`);
    });
    child.stderr?.on('data', (chunk: Buffer) => {
      process.stderr.write(`[daemon] ${chunk.toString('utf8')}`);
    });

    child.on('error', (err) => {
      logger.error({ err: err.message }, 'daemon-supervisor: spawn error');
    });

    child.on('exit', (code, signal) => {
      logger.info({ code, signal }, 'daemon-supervisor: exited');
      this.child = null;
      if (this.stopping) return;

      this.crashes += 1;
      if (this.crashes > MAX_CRASH_RESTARTS) {
        logger.error('daemon-supervisor: crash budget exhausted, giving up');
        return;
      }
      setTimeout(() => {
        if (!this.stopping) this.spawn_child();
      }, RESTART_BACKOFF_MS);
    });

    this.child = child;
  }

  /**
   * Graceful stop: send a polite signal, wait, then force-kill if needed.
   * Resolves once the child is gone.
   */
  async stop(): Promise<void> {
    if (!this.child) return;
    this.stopping = true;
    const c = this.child;

    const exited = new Promise<void>((resolve) => {
      if (c.exitCode !== null) {
        resolve();
        return;
      }
      c.once('exit', () => resolve());
    });

    if (process.platform === 'win32') {
      // Windows doesn't honour SIGTERM the way POSIX does. We try SIGTERM
      // first (which is mapped to TerminateProcess by libuv), but the real
      // graceful path is the daemon's own console-ctrl handler — it'll fire
      // when the parent process closes its console handle on quit.
      try {
        c.kill();
      } catch {
        /* swallow */
      }
    } else {
      try {
        c.kill('SIGTERM');
      } catch {
        /* swallow */
      }
    }

    const timer = new Promise<'timeout'>((resolve) =>
      setTimeout(() => resolve('timeout'), STOP_GRACE_MS),
    );
    const winner = await Promise.race([exited.then(() => 'exited' as const), timer]);

    if (winner === 'timeout') {
      logger.warn('daemon-supervisor: graceful stop timed out, force-killing');
      if (process.platform === 'win32') {
        // /T = tree, /F = force
        try {
          spawn('taskkill', ['/PID', String(c.pid ?? 0), '/T', '/F'], { windowsHide: true });
        } catch (err) {
          logger.error({ err: String(err) }, 'taskkill failed');
        }
      } else {
        try {
          c.kill('SIGKILL');
        } catch {
          /* swallow */
        }
      }
      await exited;
    }

    this.child = null;
  }
}
