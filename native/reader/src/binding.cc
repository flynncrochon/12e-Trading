// N-API binding for the Electron main process. Exposes:
//   open(): boolean        — attempt to attach to the existing shm region
//   close(): void
//   is_open(): boolean
//   poll_ticks(max_n): Tick[]
//
// Tick is shaped as { symbol_id, volume, price, ts_ns, seq } in JS — snake_case
// to match the C++ Tick struct exactly across the boundary.
// A Float64Array zero-copy variant is left as a future optimization.

#include "ShmReader.h"
#include "Tick.h"

#include <napi.h>

#include <cstdint>
#include <vector>

namespace {

td::ShmReader& reader() {
    static td::ShmReader inst;
    return inst;
}

Napi::Value Open(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), reader().open());
}

Napi::Value Close(const Napi::CallbackInfo& info) {
    reader().close();
    return info.Env().Undefined();
}

Napi::Value IsOpen(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), reader().is_open());
}

Napi::Value PollTicks(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "poll_ticks(max_n: number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    const int32_t max_raw = info[0].As<Napi::Number>().Int32Value();
    if (max_raw <= 0) {
        return Napi::Array::New(env, 0);
    }
    const std::size_t max_n = static_cast<std::size_t>(max_raw);

    std::vector<td::Tick> buf(max_n);
    const std::size_t n = reader().poll(buf.data(), max_n);

    Napi::Array out = Napi::Array::New(env, n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& t = buf[i];
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("symbol_id", Napi::Number::New(env, static_cast<double>(t.symbol_id)));
        obj.Set("volume",    Napi::Number::New(env, static_cast<double>(t.volume)));
        obj.Set("price",     Napi::Number::New(env, t.price));
        // ts_ns and seq are 64-bit; doubles cover ~2^53 cleanly which is plenty
        // for a session lifetime, but switch to BigInt later if needed.
        obj.Set("ts_ns",     Napi::Number::New(env, static_cast<double>(t.ts_ns)));
        obj.Set("seq",       Napi::Number::New(env, static_cast<double>(t.seq)));
        out.Set(static_cast<uint32_t>(i), obj);
    }
    return out;
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("open",       Napi::Function::New(env, Open));
    exports.Set("close",      Napi::Function::New(env, Close));
    exports.Set("is_open",    Napi::Function::New(env, IsOpen));
    exports.Set("poll_ticks", Napi::Function::New(env, PollTicks));
    return exports;
}

} // namespace

NODE_API_MODULE(shm_reader, Init)
