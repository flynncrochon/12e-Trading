import { app, BrowserWindow } from 'electron';
import { join } from 'node:path';
import { register_ipc_handlers } from './ipc_handlers';
import { start_yahoo_feed, stop_yahoo_feed } from './live_feed/yahoo_client';
import { logger } from './logger';

let main_window: BrowserWindow | null = null;
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

async function bootstrap(): Promise<void> {
  register_ipc_handlers();

  main_window = create_main_window();
  start_yahoo_feed(main_window);
}

app.whenReady().then(bootstrap).catch((err) => {
  logger.error({ err: String(err) }, 'bootstrap failed');
});

app.on('window-all-closed', () => {
  // We have a single-window UX. Quit on Windows/Linux; macOS users expect the app to stay alive.
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0 && !is_quitting) {
    main_window = create_main_window();
    start_yahoo_feed(main_window);
  }
});

app.on('before-quit', (event) => {
  if (is_quitting) return;
  is_quitting = true;
  event.preventDefault();

  logger.info('before-quit: stopping pipeline');
  stop_yahoo_feed();
  app.exit(0);
});
