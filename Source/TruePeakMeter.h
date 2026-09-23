#pragma once

#include <array>
#include <cmath>

class TruePeakMeter
{
public:
  static constexpr int oversampleFactor = 4;
  static constexpr int tapsPerPhase = 24;
  static constexpr int totalTaps = oversampleFactor * tapsPerPhase;

  void prepare(double newSampleRate)
  {
    sampleRate = newSampleRate;
    buildPolyphaseFilters();
    reset();
  }

  void reset() noexcept
  {
    delayLine.fill(0.0f);
    writeIndex = 0;
  }

  float process(float inputSample) noexcept
  {
    delayLine[static_cast<size_t>(writeIndex)] = inputSample;

    float maximum = 0.0f;

    for (int phase = 0; phase < oversampleFactor; ++phase)
    {
      float reconstructed = 0.0f;

      for (int tap = 0; tap < tapsPerPhase; ++tap)
      {
        int delayIndex = writeIndex - tap;

        if (delayIndex < 0)
          delayIndex += tapsPerPhase;

        reconstructed += coefficients[static_cast<size_t>(phase)]
                                     [static_cast<size_t>(tap)] *
                         delayLine[static_cast<size_t>(delayIndex)];
      }

      maximum = std::max(maximum, std::abs(reconstructed));
    }

    ++writeIndex;

    if (writeIndex >= tapsPerPhase)
      writeIndex = 0;

    return maximum;
  }

private:
  void buildPolyphaseFilters()
  {
    constexpr double pi = 3.14159265358979323846;
    constexpr int halfTaps = tapsPerPhase / 2;
    constexpr double cutoff = 0.94;

    for (int phase = 0; phase < oversampleFactor; ++phase)
    {
      const double fractionalDelay =
          static_cast<double>(phase) / static_cast<double>(oversampleFactor);

      double coefficientSum = 0.0;

      for (int tap = 0; tap < tapsPerPhase; ++tap)
      {
        const double offset = static_cast<double>(tap - (halfTaps - 1)) - fractionalDelay;

        const double x = cutoff * offset;

        const double sinc = std::abs(x) < 1.0e-12
                                ? 1.0
                                : std::sin(pi * x) / (pi * x);

        const double normalizedPosition =
            static_cast<double>(tap) / static_cast<double>(tapsPerPhase - 1);

        const double window =
            0.42 - 0.5 * std::cos(2.0 * pi * normalizedPosition) + 0.08 * std::cos(4.0 * pi * normalizedPosition);

        const double coefficient = cutoff * sinc * window;

        coefficients[static_cast<size_t>(phase)]
                    [static_cast<size_t>(tap)] =
                        static_cast<float>(coefficient);

        coefficientSum += coefficient;
      }

      if (std::abs(coefficientSum) > 1.0e-12)
      {
        for (int tap = 0; tap < tapsPerPhase; ++tap)
        {
          coefficients[static_cast<size_t>(phase)]
                      [static_cast<size_t>(tap)] =
                          static_cast<float>(
                              coefficients[static_cast<size_t>(phase)]
                                          [static_cast<size_t>(tap)] /
                              coefficientSum);
        }
      }
    }
  }

  double sampleRate = 48000.0;

  std::array<std::array<float, tapsPerPhase>, oversampleFactor> coefficients{};
  std::array<float, tapsPerPhase> delayLine{};

  int writeIndex = 0;
};