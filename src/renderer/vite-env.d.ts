/// <reference types="vite/client" />

import type { TradingApi } from '../preload';

declare global {
  interface Window {
    trading: TradingApi;
  }
}

export {};
