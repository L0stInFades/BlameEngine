// Adversarial guest: an infinite loop. Before gas metering this would HANG the host (wasm3 has no
// CPU-fuel hook); after the load-time gas instrumentation it must trap FuelExhausted once the
// per-run budget is spent. `volatile` keeps the loop observable so the optimizer cannot delete it.
extern "C" int run(int n) {
    volatile int s = 0;
    for (;;) {
        s = s + 1;
    }
    return s;  // unreachable
}
