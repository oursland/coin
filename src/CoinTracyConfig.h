// CoinTracyConfig.h — Conditional Tracy include for Coin3D
// When COIN_USE_TRACY is defined, Tracy profiling macros are available.
// Otherwise they expand to nothing.

#ifndef COIN_TRACY_CONFIG_H
#define COIN_TRACY_CONFIG_H

#ifdef COIN_USE_TRACY
#include <tracy/Tracy.hpp>
#else
#define ZoneScoped
#define ZoneScopedN(x)
#define ZoneText(x, y)
#define FrameMark
#define TracyPlot(x, y)
#endif

#endif // COIN_TRACY_CONFIG_H
