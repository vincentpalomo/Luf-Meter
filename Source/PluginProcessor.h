#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <array>

#include "KWeightingFilter.h"
#include "TruePeakMeter.h"

class LufMeterAudioProcessor final : public juce::AudioProcessor
{
public:
  LufMeterAudioProcessor();
  ~LufMeterAudioProcessor() override = default;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midiMessages) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  float getLeftPeak() const noexcept;
  float getRightPeak() const noexcept;
  float getLeftPeakHold() const noexcept;
  float getRightPeakHold() const noexcept;
  float getTruePeakHold() const noexcept;
  bool hasClipped() const noexcept;
  void requestPeakReset() noexcept;
  float getMomentaryLufs() const noexcept;
  float getShortTermLufs() const noexcept;
  float getIntegratedLufs() const noexcept;
  bool hasIntegratedMeasurement() const noexcept;
  void requestIntegratedReset() noexcept;

  float getLoudnessRange() const noexcept;
  bool hasLoudnessRangeMeasurement() const noexcept;

private:
  void resetWindows(double sampleRate);
  void clearIntegratedMeasurement() noexcept;
  void addIntegratedBlock(double meanSquare) noexcept;
  void updateIntegratedLufs();
  void updateLoudnessRange();

  void clearHistograms() noexcept;

  static int lufsToHistogramIndex(float lufs) noexcept;
  static float histogramIndexToLufs(int index) noexcept;

  static float energyToLufs(double meanSquare) noexcept;

  std::atomic<float> leftPeak{0.0f};
  std::atomic<float> rightPeak{0.0f};
  std::atomic<float> leftPeakHold{0.0f};
  std::atomic<float> rightPeakHold{0.0f};
  std::atomic<bool> clipDetected{false};
  std::atomic<bool> peakResetRequested{false};
  std::atomic<float> momentaryLufs{-100.0f};
  std::atomic<float> shortTermLufs{-100.0f};
  std::atomic<float> integratedLufs{-100.0f};
  std::atomic<float> truePeakHold{0.0f};

  TruePeakMeter leftTruePeakMeter;
  TruePeakMeter rightTruePeakMeter;

  std::atomic<bool> integratedMeasurementAvailable{false};
  std::atomic<bool> integratedResetRequested{false};

  KWeightingFilter leftKWeighting;
  KWeightingFilter rightKWeighting;

  std::vector<float> momentaryEnergy;
  std::vector<float> shortTermEnergy;

  std::atomic<float> loudnessRange{0.0f};
  std::atomic<bool> loudnessRangeAvailable{false};

  static constexpr int histogramBins = 701;
  static constexpr float histogramMinimumLufs = -70.0f;
  static constexpr float histogramBinWidth = 0.1f;

  std::array<unsigned int, histogramBins> integratedHistogram{};
  std::array<unsigned int, histogramBins> shortTermHistogram{};

  void addToHistogram(std::array<unsigned int, histogramBins> &histogram,
                      float lufs) noexcept;

  int momentaryWriteIndex = 0;
  int shortTermWriteIndex = 0;

  double momentarySum = 0.0;
  double shortTermSum = 0.0;

  int samplesSinceLastUpdate = 0;
  int updateIntervalSamples = 0;

  int samplesSinceIntegratedBlock = 0;
  int integratedBlockIntervalSamples = 0;

  int integratedUpdateCountdown = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LufMeterAudioProcessor)
};