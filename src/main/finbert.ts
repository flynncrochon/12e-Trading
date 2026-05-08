import { app } from 'electron';
import { join } from 'node:path';
import { logger } from './logger';

export interface SentimentScore {
  /** ProsusAI/finbert classes: 'positive' | 'negative' | 'neutral'. */
  label: 'positive' | 'negative' | 'neutral';
  /** Softmax probability of the predicted class, in [0, 1]. */
  score: number;
}

type Classifier = (input: string) => Promise<Array<{ label: string; score: number }>>;

let classifier: Classifier | null = null;
let load_promise: Promise<Classifier | null> | null = null;
let load_failed = false;

/**
 * Lazily loads `Xenova/finbert` (ONNX-converted ProsusAI/finbert) via
 * transformers.js. The first call triggers a one-shot model download
 * (~110 MB) into `userData/models`; subsequent calls reuse the in-memory
 * instance. If the load fails we latch into `load_failed` so callers can
 * fall back to keyword filtering instead of retrying every headline.
 */
async function get_classifier(): Promise<Classifier | null> {
  if (classifier) return classifier;
  if (load_failed) return null;
  if (load_promise) return load_promise;

  load_promise = (async () => {
    try {
      const { pipeline, env } = await import('@huggingface/transformers');
      env.cacheDir = join(app.getPath('userData'), 'models');
      env.allowLocalModels = true;

      logger.info({ cache_dir: env.cacheDir }, 'finbert: loading model');
      const t0 = Date.now();
      const pipe = await pipeline('sentiment-analysis', 'Xenova/finbert');
      logger.info({ ms: Date.now() - t0 }, 'finbert: model ready');
      classifier = pipe as unknown as Classifier;
      return classifier;
    } catch (err) {
      load_failed = true;
      logger.warn({ err: String(err) }, 'finbert: load failed; sentiment disabled');
      return null;
    } finally {
      load_promise = null;
    }
  })();

  return load_promise;
}

export async function classify_headline(title: string): Promise<SentimentScore | null> {
  const pipe = await get_classifier();
  if (!pipe) return null;
  try {
    const out = await pipe(title);
    const top = Array.isArray(out) ? out[0] : null;
    if (!top || typeof top.label !== 'string' || typeof top.score !== 'number') return null;
    const label = top.label.toLowerCase();
    if (label !== 'positive' && label !== 'negative' && label !== 'neutral') return null;
    return { label, score: top.score };
  } catch (err) {
    logger.warn({ err: String(err), title }, 'finbert: classify failed');
    return null;
  }
}

/** Pre-warm the model so the first headline doesn't pay the cold-load latency. */
export function preload_classifier(): void {
  void get_classifier();
}
