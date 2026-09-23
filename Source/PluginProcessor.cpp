#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <array>
#include <limits>

namespace
{
  constexpr float silenceFloor = -100.0f;
  constexpr float absoluteGateLufs = -70.0f;
  constexpr float relativeGateOffsetLu = -10.0f;
}

LufMeterAudioProcessor::LufMeterAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void LufMeterAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  juce::ignoreUnused(samplesPerBlock);

  leftKWeighting.prepare(sampleRate);
  rightKWeighting.prepare(sampleRate);
  leftTruePeakMeter.prepare(sampleRate);
  rightTruePeakMeter.prepare(sampleRate);
  resetWindows(sampleRate);

  leftPeak.store(0.0f, std::memory_order_relaxed);
  rightPeak.store(0.0f, std::memory_order_relaxed);
  leftPeakHold.store(0.0f, std::memory_order_relaxed);
  rightPeakHold.store(0.0f, std::memory_order_relaxed);
  truePeakHold.store(0.0f, std::memory_order_relaxed);
  clipDetected.store(false, std::memory_order_relaxed);
  peakResetRequested.store(false, std::memory_order_relaxed);
  momentaryLufs.store(silenceFloor, std::memory_order_relaxed);
  shortTermLufs.store(silenceFloor, std::memory_order_relaxed);

  clearIntegratedMeasurement();
}

void LufMeterAudioProcessor::releaseResources()
{
}

bool LufMeterAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
  const auto mainInput = layouts.getMainInputChannelSet();
  const auto mainOutput = layouts.getMainOutputChannelSet();

  if (mainInput != mainOutput)
    return false;

  return mainInput == juce::AudioChannelSet::mono() || mainInput == juce::AudioChannelSet::stereo();
}

void LufMeterAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                          juce::MidiBuffer &midiMessages)
{
  juce::ScopedNoDenormals noDenormals;
  juce::ignoreUnused(midiMessages);

  if (peakResetRequested.exchange(false, std::memory_order_relaxed))
  {
    leftPeakHold.store(0.0f, std::memory_order_relaxed);
    rightPeakHold.store(0.0f, std::memory_order_relaxed);
    truePeakHold.store(0.0f, std::memory_order_relaxed);
    clipDetected.store(false, std::memory_order_relaxed);

    leftTruePeakMeter.reset();
    rightTruePeakMeter.reset();
  }

  if (integratedResetRequested.exchange(false, std::memory_order_relaxed))
    clearIntegratedMeasurement();

  const int numChannels = buffer.getNumChannels();
  const int numSamples = buffer.getNumSamples();

  const float newLeftPeak = numChannels > 0
                                ? buffer.getMagnitude(0, 0, numSamples)
                                : 0.0f;

  const float newRightPeak = numChannels > 1
                                 ? buffer.getMagnitude(1, 0, numSamples)
                                 : newLeftPeak;

  leftPeak.store(newLeftPeak, std::memory_order_relaxed);
  rightPeak.store(newRightPeak, std::memory_order_relaxed);

  const float previousLeftHold = leftPeakHold.load(std::memory_order_relaxed);
  const float previousRightHold = rightPeakHold.load(std::memory_order_relaxed);

  if (newLeftPeak > previousLeftHold)
    leftPeakHold.store(newLeftPeak, std::memory_order_relaxed);

  if (newRightPeak > previousRightHold)
    rightPeakHold.store(newRightPeak, std::memory_order_relaxed);

  if (momentaryEnergy.empty() || shortTermEnergy.empty())
    return;

  const auto *left = numChannels > 0 ? buffer.getReadPointer(0) : nullptr;
  const auto *right = numChannels > 1 ? buffer.getReadPointer(1) : left;

  float blockTruePeak = 0.0f;

  for (int sample = 0; sample < numSamples; ++sample)
  {
    const float leftSample = left != nullptr ? left[sample] : 0.0f;
    const float rightSample = right != nullptr ? right[sample] : 0.0f;

    const float leftTruePeak = leftTruePeakMeter.process(leftSample);
    const float rightTruePeak = rightTruePeakMeter.process(rightSample);

    blockTruePeak = juce::jmax(blockTruePeak,
                               leftTruePeak,
                               rightTruePeak);

    const float weightedLeft = leftKWeighting.process(leftSample);
    const float weightedRight = rightKWeighting.process(rightSample);

    const float stereoEnergy = (weightedLeft * weightedLeft) + (weightedRight * weightedRight);

    momentarySum -= momentaryEnergy[static_cast<size_t>(momentaryWriteIndex)];
    momentaryEnergy[static_cast<size_t>(momentaryWriteIndex)] = stereoEnergy;
    momentarySum += stereoEnergy;

    ++momentaryWriteIndex;
    if (momentaryWriteIndex >= static_cast<int>(momentaryEnergy.size()))
      momentaryWriteIndex = 0;

    shortTermSum -= shortTermEnergy[static_cast<size_t>(shortTermWriteIndex)];
    shortTermEnergy[static_cast<size_t>(shortTermWriteIndex)] = stereoEnergy;
    shortTermSum += stereoEnergy;

    ++shortTermWriteIndex;
    if (shortTermWriteIndex >= static_cast<int>(shortTermEnergy.size()))
      shortTermWriteIndex = 0;

    ++samplesSinceLastUpdate;
    ++samplesSinceIntegratedBlock;

    if (samplesSinceIntegratedBlock >= integratedBlockIntervalSamples)
    {
      samplesSinceIntegratedBlock = 0;

      const double blockMeanSquare = momentarySum / static_cast<double>(momentaryEnergy.size());

      addIntegratedBlock(blockMeanSquare);
    }
  }

  const float previousTruePeakHold =
      truePeakHold.load(std::memory_order_relaxed);

  if (blockTruePeak > previousTruePeakHold)
    truePeakHold.store(blockTruePeak, std::memory_order_relaxed);

  if (newLeftPeak >= 1.0f || newRightPeak >= 1.0f || blockTruePeak >= 1.0f)
  {
    clipDetected.store(true, std::memory_order_relaxed);
  }

  if (samplesSinceLastUpdate >= updateIntervalSamples)
  {
    samplesSinceLastUpdate = 0;

    const double momentaryMeanSquare = momentarySum / static_cast<double>(momentaryEnergy.size());

    const double shortTermMeanSquare = shortTermSum / static_cast<double>(shortTermEnergy.size());

    const float newMomentaryLufs = energyToLufs(momentaryMeanSquare);
    const float newShortTermLufs = energyToLufs(shortTermMeanSquare);

    momentaryLufs.store(newMomentaryLufs, std::memory_order_relaxed);
    shortTermLufs.store(newShortTermLufs, std::memory_order_relaxed);

    addToHistogram(shortTermHistogram, newShortTermLufs);
  }

  if (--integratedUpdateCountdown <= 0)
  {
    integratedUpdateCountdown = 10;

    updateIntegratedLufs();
    updateLoudnessRange();
  }
}

juce::AudioProcessorEditor *LufMeterAudioProcessor::createEditor()
{
  return new LufMeterAudioProcessorEditor(*this);
}

bool LufMeterAudioProcessor::hasEditor() const
{
  return true;
}

const juce::String LufMeterAudioProcessor::getName() const
{
  return JucePlugin_Name;
}

bool LufMeterAudioProcessor::acceptsMidi() const
{
  return false;
}

bool LufMeterAudioProcessor::producesMidi() const
{
  return false;
}

bool LufMeterAudioProcessor::isMidiEffect() const
{
  return false;
}

double LufMeterAudioProcessor::getTailLengthSeconds() const
{
  return 0.0;
}

int LufMeterAudioProcessor::getNumPrograms()
{
  return 1;
}

int LufMeterAudioProcessor::getCurrentProgram()
{
  return 0;
}

void LufMeterAudioProcessor::setCurrentProgram(int index)
{
  juce::ignoreUnused(index);
}

const juce::String LufMeterAudioProcessor::getProgramName(int index)
{
  juce::ignoreUnused(index);
  return {};
}

void LufMeterAudioProcessor::changeProgramName(int index,
                                               const juce::String &newName)
{
  juce::ignoreUnused(index, newName);
}

void LufMeterAudioProcessor::getStateInformation(juce::MemoryBlock &destData)
{
  juce::ignoreUnused(destData);
}

void LufMeterAudioProcessor::setStateInformation(const void *data,
                                                 int sizeInBytes)
{
  juce::ignoreUnused(data, sizeInBytes);
}

float LufMeterAudioProcessor::getLeftPeak() const noexcept
{
  return leftPeak.load(std::memory_order_relaxed);
}

float LufMeterAudioProcessor::getRightPeak() const noexcept
{
  return rightPeak.load(std::memory_order_relaxed);
}

float LufMeterAudioProcessor::getLeftPeakHold() const noexcept
{
  return leftPeakHold.load(std::memory_order_relaxed);
}

float LufMeterAudioProcessor::getRightPeakHold() const noexcept
{
  return rightPeakHold.load(std::memory_order_relaxed);
}

float LufMeterAudioProcessor::getTruePeakHold() const noexcept
{
  return truePeakHold.load(std::memory_order_relaxed);
}

bool LufMeterAudioProcessor::hasClipped() const noexcept
{
  return clipDetected.load(std::memory_order_relaxed);
}

void LufMeterAudioProcessor::requestPeakReset() noexcept
{
  peakResetRequested.store(true, std::memory_order_relaxed);
}

float LufMeterAudioProcessor::getMomentaryLufs() const noexcept
{
  return momentaryLufs.load(std::memory_order_relaxed);
}

float LufMeterAudioProcessor::getShortTermLufs() const noexcept
{
  return shortTermLufs.load(std::memory_order_relaxed);
}

float LufMeterAudioProcessor::getIntegratedLufs() const noexcept
{
  return integratedLufs.load(std::memory_order_relaxed);
}

bool LufMeterAudioProcessor::hasIntegratedMeasurement() const noexcept
{
  return integratedMeasurementAvailable.load(std::memory_order_relaxed);
}

float LufMeterAudioProcessor::getLoudnessRange() const noexcept
{
  return loudnessRange.load(std::memory_order_relaxed);
}

bool LufMeterAudioProcessor::hasLoudnessRangeMeasurement() const noexcept
{
  return loudnessRangeAvailable.load(std::memory_order_relaxed);
}

void LufMeterAudioProcessor::requestIntegratedReset() noexcept
{
  integratedResetRequested.store(true, std::memory_order_relaxed);
}

void LufMeterAudioProcessor::resetWindows(double sampleRate)
{
  const int momentarySamples = juce::jmax(1, juce::roundToInt(sampleRate * 0.4));
  const int shortTermSamples = juce::jmax(1, juce::roundToInt(sampleRate * 3.0));

  momentaryEnergy.assign(static_cast<size_t>(momentarySamples), 0.0f);
  shortTermEnergy.assign(static_cast<size_t>(shortTermSamples), 0.0f);

  clearHistograms();

  momentaryWriteIndex = 0;
  shortTermWriteIndex = 0;

  momentarySum = 0.0;
  shortTermSum = 0.0;

  samplesSinceLastUpdate = 0;
  updateIntervalSamples = juce::jmax(1, juce::roundToInt(sampleRate / 20.0));

  samplesSinceIntegratedBlock = 0;
  integratedBlockIntervalSamples = juce::jmax(1, juce::roundToInt(sampleRate * 0.1));

  integratedUpdateCountdown = 10;
}

void LufMeterAudioProcessor::clearIntegratedMeasurement() noexcept
{
  clearHistograms();

  integratedLufs.store(silenceFloor, std::memory_order_relaxed);
  integratedMeasurementAvailable.store(false, std::memory_order_relaxed);

  loudnessRange.store(0.0f, std::memory_order_relaxed);
  loudnessRangeAvailable.store(false, std::memory_order_relaxed);

  integratedUpdateCountdown = 1;
}

void LufMeterAudioProcessor::addIntegratedBlock(double meanSquare) noexcept
{
  addToHistogram(integratedHistogram, energyToLufs(meanSquare));
}

void LufMeterAudioProcessor::updateIntegratedLufs()
{
  unsigned long long absoluteGatedCount = 0;
  double absoluteGatedEnergySum = 0.0;

  for (int index = 0; index < histogramBins; ++index)
  {
    const auto count = integratedHistogram[static_cast<size_t>(index)];

    if (count == 0)
      continue;

    const float lufs = histogramIndexToLufs(index);
    const double energy = std::pow(10.0, (lufs + 0.691) / 10.0);

    absoluteGatedEnergySum += energy * static_cast<double>(count);
    absoluteGatedCount += count;
  }

  if (absoluteGatedCount == 0)
  {
    integratedLufs.store(silenceFloor, std::memory_order_relaxed);
    integratedMeasurementAvailable.store(false, std::memory_order_relaxed);
    return;
  }

  const double absoluteGatedMeanSquare = absoluteGatedEnergySum / static_cast<double>(absoluteGatedCount);

  const float absoluteGatedLufs = energyToLufs(absoluteGatedMeanSquare);
  const float relativeGateLufs = absoluteGatedLufs + relativeGateOffsetLu;

  unsigned long long relativeGatedCount = 0;
  double relativeGatedEnergySum = 0.0;

  for (int index = 0; index < histogramBins; ++index)
  {
    const auto count = integratedHistogram[static_cast<size_t>(index)];

    if (count == 0)
      continue;

    const float lufs = histogramIndexToLufs(index);

    if (lufs < relativeGateLufs)
      continue;

    const double energy = std::pow(10.0, (lufs + 0.691) / 10.0);

    relativeGatedEnergySum += energy * static_cast<double>(count);
    relativeGatedCount += count;
  }

  if (relativeGatedCount == 0)
  {
    integratedLufs.store(silenceFloor, std::memory_order_relaxed);
    integratedMeasurementAvailable.store(false, std::memory_order_relaxed);
    return;
  }

  const double integratedMeanSquare = relativeGatedEnergySum / static_cast<double>(relativeGatedCount);

  integratedLufs.store(energyToLufs(integratedMeanSquare),
                       std::memory_order_relaxed);

  integratedMeasurementAvailable.store(true, std::memory_order_relaxed);
}

void LufMeterAudioProcessor::updateLoudnessRange()
{
  unsigned long long absoluteGatedCount = 0;
  double absoluteGatedEnergySum = 0.0;

  for (int index = 0; index < histogramBins; ++index)
  {
    const auto count = shortTermHistogram[static_cast<size_t>(index)];

    if (count == 0)
      continue;

    const float lufs = histogramIndexToLufs(index);
    const double energy = std::pow(10.0, (lufs + 0.691) / 10.0);

    absoluteGatedEnergySum += energy * static_cast<double>(count);
    absoluteGatedCount += count;
  }

  if (absoluteGatedCount < 2)
  {
    loudnessRange.store(0.0f, std::memory_order_relaxed);
    loudnessRangeAvailable.store(false, std::memory_order_relaxed);
    return;
  }

  const double absoluteGatedMeanSquare = absoluteGatedEnergySum / static_cast<double>(absoluteGatedCount);

  const float absoluteGatedLufs = energyToLufs(absoluteGatedMeanSquare);
  const float relativeGateLufs = absoluteGatedLufs - 20.0f;

  unsigned long long relativeGatedCount = 0;

  for (int index = 0; index < histogramBins; ++index)
  {
    const auto count = shortTermHistogram[static_cast<size_t>(index)];

    if (count == 0)
      continue;

    if (histogramIndexToLufs(index) >= relativeGateLufs)
      relativeGatedCount += count;
  }

  if (relativeGatedCount < 2)
  {
    loudnessRange.store(0.0f, std::memory_order_relaxed);
    loudnessRangeAvailable.store(false, std::memory_order_relaxed);
    return;
  }

  const auto findPercentile = [this, relativeGateLufs, relativeGatedCount](float fraction)
  {
    const auto targetCount = static_cast<unsigned long long>(
        std::ceil(fraction * static_cast<float>(relativeGatedCount)));

    unsigned long long accumulatedCount = 0;

    for (int index = 0; index < histogramBins; ++index)
    {
      const auto count = shortTermHistogram[static_cast<size_t>(index)];

      if (count == 0)
        continue;

      if (histogramIndexToLufs(index) < relativeGateLufs)
        continue;

      accumulatedCount += count;

      if (accumulatedCount >= targetCount)
        return histogramIndexToLufs(index);
    }

    return histogramIndexToLufs(histogramBins - 1);
  };

  const float lowPercentile = findPercentile(0.10f);
  const float highPercentile = findPercentile(0.95f);

  loudnessRange.store(highPercentile - lowPercentile,
                      std::memory_order_relaxed);

  loudnessRangeAvailable.store(true, std::memory_order_relaxed);
}

void LufMeterAudioProcessor::clearHistograms() noexcept
{
  integratedHistogram.fill(0);
  shortTermHistogram.fill(0);
}

void LufMeterAudioProcessor::addToHistogram(
    std::array<unsigned int, histogramBins> &histogram,
    float lufs) noexcept
{
  if (lufs < histogramMinimumLufs)
    return;

  const int index = lufsToHistogramIndex(lufs);

  auto &count = histogram[static_cast<size_t>(index)];

  if (count < std::numeric_limits<unsigned int>::max())
    ++count;
}

int LufMeterAudioProcessor::lufsToHistogramIndex(float lufs) noexcept
{
  const float clamped = juce::jlimit(histogramMinimumLufs, 0.0f, lufs);

  return juce::jlimit(
      0,
      histogramBins - 1,
      juce::roundToInt((clamped - histogramMinimumLufs) / histogramBinWidth));
}

float LufMeterAudioProcessor::histogramIndexToLufs(int index) noexcept
{
  return histogramMinimumLufs + static_cast<float>(index) * histogramBinWidth;
}

float LufMeterAudioProcessor::energyToLufs(double meanSquare) noexcept
{
  if (meanSquare <= 1.0e-12)
    return silenceFloor;

  return static_cast<float>(-0.691 + 10.0 * std::log10(meanSquare));
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
  return new LufMeterAudioProcessor();
}