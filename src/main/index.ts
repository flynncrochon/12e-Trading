import { app, BrowserWindow, Menu } from 'electron';
import { join } from 'node:path';
import { DaemonSupervisor } from './daemon_supervisor';
import { event_bridge } from './event_bridge';
import { register_ipc_handlers } from './ipc_handlers';
import { logger } from './logger';
import { resolve_daemon_path } from './paths';
import { attach_to_daemon, start_poll_loop, stop_poll_loop } from './tick_bridge';

let main_window: BrowserWindow | null = null;
let supervisor: DaemonSupervisor | null = null;
let event_port: number | null = null;
let is_quitting = false;

function create_main_window(): BrowserWindow {
  const win = new BrowserWindow({
    width: 960,
    height: 720,
    minWidth: 480,
    minHeight: 360,
    show: false,
    backgroundColor: '#0d1117',
    title: '12e Trading',
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });

  win.once('ready-to-show', () => win.show());

  if (process.env.ELECTRON_RENDERER_URL) {
    win.loadURL(process.env.ELECTRON_RENDERER_URL);
    win.webContents.openDevTools({ mode: 'detach' });
  } else {
    win.loadFile(join(__dirname, '../renderer/index.html'));
  }

  win.webContents.on('did-fail-load', (_e, error_code, error_description, validated_url) => {
    logger.error(
      { error_code, error_description, validated_url },
      'renderer: did-fail-load',
    );
  });
  win.webContents.on('render-process-gone', (_e, details) => {
    logger.error({ details }, 'renderer: render-process-gone');
  });

  return win;
}

/**
 * Brings the full data pipeline up for a fresh window:
 *   1. Attach the EventBridge listener (already started in bootstrap()).
 *   2. Spawn the C++ market-data-service with --event-port=N — the daemon
 *      runs the hot quote feed AND the one-shot history backfill + summary,
 *      pushing all results to the renderer (ticks via SHM, history/summary
 *      via the loopback event channel).
 *   3. Attach the N-API shm reader and forward batched ticks over IPC.
 */
async function start_pipeline(window: BrowserWindow): Promise<void> {
  event_bridge.set_window(window);
  event_bridge.clear_caches();

  if (event_port === null) {
    logger.error('pipeline: event_bridge port not initialised');
    return;
  }

  supervisor = new DaemonSupervisor(resolve_daemon_path(), [`--event-port=${event_port}`]);
  supervisor.start();

  const attached = await attach_to_daemon();
  if (attached) {
    start_poll_loop(window);
  } else {
    logger.error('pipeline: failed to attach to daemon shared memory');
  }
}

async function bootstrap(): Promise<void> {
  Menu.setApplicationMenu(null);
  register_ipc_handlers();
  // Listener has to come up before the daemon spawn so the daemon's
  // connect() call has somewhere to land.
  event_port = await event_bridge.start();
  main_window = create_main_window();
  await start_pipeline(main_window);
}

app.whenReady().then(bootstrap).catch((err) => {
  logger.error({ err: String(err) }, 'bootstrap failed');
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0 && !is_quitting) {
    main_window = create_main_window();
    start_pipeline(main_window).catch((err) => {
      logger.error({ err: String(err) }, 'reactivate pipeline failed');
    });
  }
});

app.on('before-quit', (event) => {
  if (is_quitting) return;
  is_quitting = true;
  event.preventDefault();

  logger.info('before-quit: stopping pipeline');
  stop_poll_loop();
  const stop_supervisor = supervisor ? supervisor.stop() : Promise.resolve();
  Promise.allSettled([stop_supervisor, event_bridge.stop()])
    .then((results) => {
      for (const r of results) {
        if (r.status === 'rejected') {
          logger.error({ err: String(r.reason) }, 'before-quit: shutdown failed');
        }
      }
    })
    .finally(() => app.exit(0));
});
