// Bounded-work guest: a counted loop of `n` iterations. Used to show fuel accounting scales with
// real work — it COMPLETES under an ample budget (returning sum 0..n-1) and TRAPS FuelExhausted
// under a tight one. `volatile` defeats the optimizer's closed-form (Gauss) rewrite so the loop,
// and thus the per-iteration fuel charge, actually runs.
extern "C" int run(int n) {
    volatile int s = 0;
    for (int i = 0; i < n; ++i) {
        s = s + i;
    }
    return s;
}
