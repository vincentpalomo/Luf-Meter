#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class LufMeterAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
  explicit LufMeterAudioProcessorEditor(LufMeterAudioProcessor &);
  ~LufMeterAudioProcessorEditor() override = default;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  void timerCallback() override;
  void resetIntegratedMeasurement();
  void resetPeakMeasurement();

  static float gainToDecibels(float gain) noexcept;
  static juce::Colour colourForPeak(float db) noexcept;
  static juce::String loudnessText(float value);

  LufMeterAudioProcessor &audioProcessor;

  juce::TextButton resetButton{"RESET"};
  juce::TextButton resetPeaksButton{"RESET PEAKS"};

  float leftPeakDb = -100.0f;
  float rightPeakDb = -100.0f;
  float leftPeakHoldDb = -100.0f;
  float rightPeakHoldDb = -100.0f;
  float truePeakHoldDb = -100.0f;
  bool clipDetected = false;
  float momentaryLufs = -100.0f;
  float shortTermLufs = -100.0f;
  float integratedLufs = -100.0f;
  bool integratedMeasurementAvailable = false;
  float loudnessRange = 0.0f;
  bool loudnessRangeAvailable = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LufMeterAudioProcessorEditor)
};