#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>

/**
    Colour maps for the spectrogram.

    All of these are monotonic in lightness, so level reads correctly whatever
    the hue — a rainbow map can't claim that, which is why analysers that use
    one are so hard to read quantitatively.

    Convenient coincidence: an amber phosphor ramp is *naturally* monotonic in
    lightness, because a real CRT's brightness and colour rise together as the
    beam drives harder. The period-correct look and the perceptually correct
    look are the same thing here.
*/
namespace ColourMaps
{
    struct Stop { float position; juce::uint8 r, g, b; };

    /** Default. Cold black through rust and orange to white-hot, the way an
        amber tube actually behaves when you overdrive it.
    */
    inline constexpr std::array<Stop, 8> amberPhosphor
    {{
        { 0.000f,   6,   4,   4 },
        { 0.150f,  38,  14,  12 },
        { 0.300f,  84,  26,  14 },
        { 0.450f, 140,  46,  12 },
        { 0.600f, 196,  82,  14 },
        { 0.750f, 238, 133,  26 },
        { 0.880f, 252, 190,  92 },
        { 1.000f, 255, 244, 214 },
    }};

    /** P1 phosphor — the Nostromo green. */
    inline constexpr std::array<Stop, 6> greenPhosphor
    {{
        { 0.000f,   4,   6,   5 },
        { 0.200f,  10,  40,  26 },
        { 0.400f,  16,  80,  46 },
        { 0.600f,  30, 140,  74 },
        { 0.800f,  86, 206, 120 },
        { 1.000f, 214, 255, 222 },
    }};

    inline constexpr std::array<Stop, 9> magma
    {{
        { 0.000f,   0,   0,   4 },
        { 0.125f,  28,  16,  68 },
        { 0.250f,  79,  18, 123 },
        { 0.375f, 129,  37, 129 },
        { 0.500f, 181,  54, 122 },
        { 0.625f, 229,  80, 100 },
        { 0.750f, 251, 135,  97 },
        { 0.875f, 254, 194, 135 },
        { 1.000f, 252, 253, 191 },
    }};

    /** Maps 0..1 to a colour, interpolating between stops. */
    template <size_t NumStops>
    inline juce::PixelARGB lookup (const std::array<Stop, NumStops>& map, float value) noexcept
    {
        const auto clamped = juce::jlimit (0.0f, 1.0f, value);

        for (size_t i = 1; i < NumStops; ++i)
        {
            const auto& hi = map[i];

            if (clamped > hi.position && i + 1 < NumStops)
                continue;

            const auto& lo = map[i - 1];
            const auto span = hi.position - lo.position;
            const auto t = span > 0.0f ? (clamped - lo.position) / span : 0.0f;

            const auto blend = [t] (juce::uint8 a, juce::uint8 b)
            {
                return static_cast<juce::uint8> (juce::jlimit (0.0f, 255.0f,
                    a + (b - a) * juce::jlimit (0.0f, 1.0f, t)));
            };

            return juce::PixelARGB (255, blend (lo.r, hi.r), blend (lo.g, hi.g), blend (lo.b, hi.b));
        }

        const auto& last = map[NumStops - 1];
        return juce::PixelARGB (255, last.r, last.g, last.b);
    }

    /** Precomputes a 256-entry table, so per-pixel lookup is an array index. */
    template <size_t NumStops>
    inline std::array<juce::PixelARGB, 256> buildTable (const std::array<Stop, NumStops>& map)
    {
        std::array<juce::PixelARGB, 256> table {};

        for (size_t i = 0; i < table.size(); ++i)
            table[i] = lookup (map, static_cast<float> (i) / 255.0f);

        return table;
    }
}
