#pragma once
#include <algorithm>
#include <cmath>
namespace still::dsp
{
struct OpenGains { float body; float swirl; };
inline OpenGains openGains(float open) noexcept
{
    const auto x = std::clamp(open, 0.0f, 1.0f);
    return { 0.34f + 0.66f * std::pow(x, 0.9f), std::pow(x, 1.65f) };
}
}
