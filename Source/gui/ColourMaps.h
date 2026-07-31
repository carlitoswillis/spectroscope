#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>

/**
    Perceptually-ordered colour maps for the spectrogram.

    Magma is the default because it's monotonic in lightness — level reads
    correctly whatever the hue, which a rainbow map can't claim, and it stays
    legible against a dark UI.
*/
namespace ColourMaps
{
    struct Stop { float position; juce::uint8 r, g, b; };

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
