import { type BrowserWindow } from 'electron';
import * as net from 'node:net';
import { logger } from './logger';

const HEADER_BYTES = 4;
const MAX_FRAME_BYTES = 64 * 1024 * 1024; // 64 MB safety cap

export interface BackfillPoint {
  t: number;
  price: number;
}

export interface BackfillBatch {
  symbol_id: number;
  points: BackfillPoint[];
}

export interface SymbolSummary {
  symbol_id: number;
  month_ago_price: number;
  month_ago_t: number;
}

interface DaemonEvent {
  type: string;
  // additional per-type fields
  [key: string]: unknown;
}

/**
 * TCP listener that accepts a connection from the C++ daemon and parses
 * length-prefixed JSON event frames pushed by the daemon's history backfill
 * and summary fetchers. Forwards each event to the renderer over IPC and
 * caches the latest one per (type, symbol_id) so renderers that mount after
 * the initial push can pull it via the IPC query handlers.
 *
 * Wire format: [uint32 length, little-endian][N bytes of UTF-8 JSON]
 *
 * Single connection at a time; if the daemon crashes and reconnects, the
 * old socket is dropped and the new one takes over.
 */
class EventBridge {
  private server: net.Server | null = null;
  private current_socket: net.Socket | null = null;
  private read_buffer: Buffer = Buffer.alloc(0);

  private backfill_cache = new Map<number, BackfillBatch>();
  private summary_cache = new Map<number, SymbolSummary>();

  private window: BrowserWindow | null = null;

  set_window(window: BrowserWindow): void {
    this.window = window;
  }

  /**
   * Starts listening on 127.0.0.1 on an ephemeral port. Returns the chosen
   * port so the caller can pass it to the daemon as --event-port=N.
   */
  start(): Promise<number> {
    return new Promise((resolve, reject) => {
      const server = net.createServer((socket) => this.on_connection(socket));
      server.on('error', (err) => {
        logger.error({ err: String(err) }, 'event-bridge: server error');
      });
      server.listen({ host: '127.0.0.1', port: 0 }, () => {
        const addr = server.address();
        if (!addr || typeof addr === 'string') {
          reject(new Error('event-bridge: failed to obtain listen address'));
          return;
        }
        this.server = server;
        logger.info({ port: addr.port }, 'event-bridge: listening');
        resolve(addr.port);
      });
    });
  }

  stop(): Promise<void> {
    return new Promise((resolve) => {
      this.current_socket?.destroy();
      this.current_socket = null;
      if (!this.server) {
        resolve();
        return;
      }
      this.server.close(() => {
        this.server = null;
        resolve();
      });
    });
  }

  get_backfill_cache(): BackfillBatch[] {
    return Array.from(this.backfill_cache.values());
  }

  get_summary_cache(): SymbolSummary[] {
    return Array.from(this.summary_cache.values());
  }

  clear_caches(): void {
    this.backfill_cache.clear();
    this.summary_cache.clear();
  }

  private on_connection(socket: net.Socket): void {
    if (this.current_socket) {
      logger.warn('event-bridge: replacing existing daemon connection');
      this.current_socket.destroy();
    }
    this.current_socket = socket;
    this.read_buffer = Buffer.alloc(0);

    logger.info({ remote: socket.remoteAddress, port: socket.remotePort }, 'event-bridge: connected');

    socket.on('data', (chunk) => this.on_data(chunk));
    socket.on('error', (err) => {
      logger.warn({ err: String(err) }, 'event-bridge: socket error');
    });
    socket.on('close', () => {
      if (this.current_socket === socket) {
        this.current_socket = null;
        this.read_buffer = Buffer.alloc(0);
      }
      logger.info('event-bridge: daemon disconnected');
    });
  }

  private on_data(chunk: Buffer): void {
    this.read_buffer = Buffer.concat([this.read_buffer, chunk]);
    while (this.read_buffer.length >= HEADER_BYTES) {
      const len = this.read_buffer.readUInt32LE(0);
      if (len > MAX_FRAME_BYTES) {
        logger.error({ len }, 'event-bridge: oversized frame, closing connection');
        this.current_socket?.destroy();
        this.read_buffer = Buffer.alloc(0);
        return;
      }
      if (this.read_buffer.length < HEADER_BYTES + len) return;
      const payload = this.read_buffer.subarray(HEADER_BYTES, HEADER_BYTES + len);
      this.read_buffer = this.read_buffer.subarray(HEADER_BYTES + len);
      this.dispatch(payload);
    }
  }

  private dispatch(payload: Buffer): void {
    let event: DaemonEvent;
    try {
      event = JSON.parse(payload.toString('utf8')) as DaemonEvent;
    } catch (err) {
      logger.warn({ err: String(err) }, 'event-bridge: malformed JSON frame');
      return;
    }

    switch (event.type) {
      case 'history:backfill': {
        const batch: BackfillBatch = {
          symbol_id: event.symbol_id as number,
          points: (event.points as BackfillPoint[]) ?? [],
        };
        this.backfill_cache.set(batch.symbol_id, batch);
        if (this.window && !this.window.isDestroyed()) {
          this.window.webContents.send('history:backfill', batch);
        }
        return;
      }
      case 'summary:update': {
        const summary: SymbolSummary = {
          symbol_id: event.symbol_id as number,
          month_ago_price: event.month_ago_price as number,
          month_ago_t: event.month_ago_t as number,
        };
        this.summary_cache.set(summary.symbol_id, summary);
        if (this.window && !this.window.isDestroyed()) {
          this.window.webContents.send('summary:update', summary);
        }
        return;
      }
      default:
        logger.warn({ type: event.type }, 'event-bridge: unknown event type');
    }
  }
}

/**
 * Module-level singleton — both `index.ts` (for lifecycle + window wiring)
 * and `ipc_handlers.ts` (for cache queries) reach for the same instance.
 */
export const event_bridge = new EventBridge();
