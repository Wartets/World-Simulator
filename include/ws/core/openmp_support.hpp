#pragma once

// Optional OpenMP helpers for hot cell-wise loops. When OpenMP is disabled or
// unavailable, these expand to nothing and preserve the serial execution path.
#if defined(WS_ENABLE_OPENMP) && WS_ENABLE_OPENMP
#define WS_OMP_STRINGIFY_IMPL(x) #x
#define WS_OMP_STRINGIFY(x) WS_OMP_STRINGIFY_IMPL(x)
#define WS_OMP_PRAGMA(x) _Pragma(WS_OMP_STRINGIFY(x))
#define WS_OMP_PARALLEL_FOR WS_OMP_PRAGMA(omp parallel for schedule(static))
#define WS_OMP_PARALLEL_FOR_REDUCTION_SUM(var) WS_OMP_PRAGMA(omp parallel for schedule(static) reduction(+:var))
#else
#define WS_OMP_PARALLEL_FOR
#define WS_OMP_PARALLEL_FOR_REDUCTION_SUM(var)
#endif
