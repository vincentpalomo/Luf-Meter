#pragma once

#include <cmath>

class KWeightingFilter
{
public:
  void prepare(double newSampleRate)
  {
    sampleRate = newSampleRate;

    configureHighShelf();
    configureHighPass();

    reset();
  }

  void reset() noexcept
  {
    shelf.reset();
    highPass.reset();
  }

  float process(float sample) noexcept
  {
    return highPass.process(shelf.process(sample));
  }

private:
  struct Biquad
  {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;

    void reset() noexcept
    {
      x1 = 0.0f;
      x2 = 0.0f;
      y1 = 0.0f;
      y2 = 0.0f;
    }

    float process(float x) noexcept
    {
      const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;

      return y;
    }

    void setCoefficients(double newB0,
                         double newB1,
                         double newB2,
                         double newA0,
                         double newA1,
                         double newA2)
    {
      b0 = static_cast<float>(newB0 / newA0);
      b1 = static_cast<float>(newB1 / newA0);
      b2 = static_cast<float>(newB2 / newA0);
      a1 = static_cast<float>(newA1 / newA0);
      a2 = static_cast<float>(newA2 / newA0);
    }
  };

  void configureHighShelf()
  {
    constexpr double frequency = 1681.974450955533;
    constexpr double gainDb = 3.999843853973347;
    constexpr double q = 0.7071752369554196;

    const double omega = 2.0 * pi * frequency / sampleRate;
    const double alpha = std::sin(omega) / (2.0 * q);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double cosine = std::cos(omega);
    const double beta = 2.0 * std::sqrt(a) * alpha;

    const double b0 = a * ((a + 1.0) + (a - 1.0) * cosine + beta);
    const double b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosine);
    const double b2 = a * ((a + 1.0) + (a - 1.0) * cosine - beta);

    const double a0 = (a + 1.0) - (a - 1.0) * cosine + beta;
    const double a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosine);
    const double a2 = (a + 1.0) - (a - 1.0) * cosine - beta;

    shelf.setCoefficients(b0, b1, b2, a0, a1, a2);
  }

  void configureHighPass()
  {
    constexpr double frequency = 38.13547087602444;
    constexpr double q = 0.5003270373238773;

    const double omega = 2.0 * pi * frequency / sampleRate;
    const double alpha = std::sin(omega) / (2.0 * q);
    const double cosine = std::cos(omega);

    const double b0 = (1.0 + cosine) / 2.0;
    const double b1 = -(1.0 + cosine);
    const double b2 = (1.0 + cosine) / 2.0;

    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cosine;
    const double a2 = 1.0 - alpha;

    highPass.setCoefficients(b0, b1, b2, a0, a1, a2);
  }

  static constexpr double pi = 3.14159265358979323846;

  double sampleRate = 48000.0;

  Biquad shelf;
  Biquad highPass;
};