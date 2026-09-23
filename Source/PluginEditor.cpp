#include "PluginEditor.h"

#include <cmath>

namespace
{
  constexpr float minimumPeakDb = -60.0f;
  constexpr float maximumPeakDb = 0.0f;
}

LufMeterAudioProcessorEditor::LufMeterAudioProcessorEditor(
    LufMeterAudioProcessor &processor)
    : AudioProcessorEditor(&processor),
      audioProcessor(processor)
{
  setSize(370, 680);
  setResizable(true, true);
  setResizeLimits(320, 520, 720, 1200);

  resetButton.onClick = [this]
  {
    resetIntegratedMeasurement();
  };

  resetButton.setColour(juce::TextButton::buttonColourId,
                        juce::Colour::fromRGB(45, 52, 63));

  resetButton.setColour(juce::TextButton::textColourOffId,
                        juce::Colour::fromRGB(225, 230, 238));

  addAndMakeVisible(resetButton);

  startTimerHz(30);

  resetPeaksButton.onClick = [this]
  {
    resetPeakMeasurement();
  };

  resetPeaksButton.setColour(juce::TextButton::buttonColourId,
                             juce::Colour::fromRGB(45, 52, 63));

  resetPeaksButton.setColour(juce::TextButton::textColourOffId,
                             juce::Colour::fromRGB(225, 230, 238));

  addAndMakeVisible(resetPeaksButton);
}

void LufMeterAudioProcessorEditor::paint(juce::Graphics &g)
{
  const auto background = juce::Colour::fromRGB(15, 17, 21);
  const auto panel = juce::Colour::fromRGB(27, 31, 38);
  const auto border = juce::Colour::fromRGB(65, 72, 84);
  const auto primaryText = juce::Colour::fromRGB(231, 235, 241);
  const auto secondaryText = juce::Colour::fromRGB(145, 154, 169);
  const auto accent = juce::Colour::fromRGB(112, 210, 163);

  g.fillAll(background);

  auto bounds = getLocalBounds().reduced(14);

  g.setColour(panel);
  g.fillRoundedRectangle(bounds.toFloat(), 12.0f);

  g.setColour(border);
  g.drawRoundedRectangle(bounds.toFloat(), 12.0f, 1.0f);

  bounds = bounds.reduced(14);

  auto header = bounds.removeFromTop(58);

  g.setColour(primaryText);
  g.setFont(juce::FontOptions(27.0f, juce::Font::bold));
  g.drawFittedText("LUF METER",
                   header.removeFromTop(34),
                   juce::Justification::centred,
                   1);

  g.setColour(secondaryText);
  g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
  g.drawFittedText("LOUDNESS WINDOW PREVIEW",
                   header,
                   juce::Justification::centred,
                   1);

  bounds.removeFromTop(10);

  auto integratedArea = bounds.removeFromTop(112);

  g.setColour(juce::Colour::fromRGB(21, 24, 30));
  g.fillRoundedRectangle(integratedArea.toFloat(), 10.0f);

  g.setColour(juce::Colour::fromRGB(75, 82, 94));
  g.drawRoundedRectangle(integratedArea.toFloat(), 10.0f, 1.0f);

  auto integratedTextArea = integratedArea.reduced(8);

  g.setColour(juce::Colour::fromRGB(150, 160, 175));
  g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
  g.drawFittedText("INTEGRATED",
                   integratedTextArea.removeFromTop(22),
                   juce::Justification::centred,
                   1);

  g.setColour(juce::Colour::fromRGB(112, 210, 163));
  g.setFont(juce::FontOptions(31.0f, juce::Font::bold));

  const auto integratedText = integratedMeasurementAvailable
                                  ? loudnessText(integratedLufs)
                                  : juce::String("MEASURING...");

  g.drawFittedText(integratedText,
                   integratedTextArea.removeFromTop(50),
                   juce::Justification::centred,
                   1);

  g.setColour(juce::Colour::fromRGB(139, 148, 161));
  g.setFont(juce::FontOptions(10.0f));
  g.drawFittedText("Gated programme loudness",
                   integratedTextArea,
                   juce::Justification::centred,
                   1);

  bounds.removeFromTop(10);

  auto analysisRow = bounds.removeFromTop(72);

  const int analysisGap = 10;
  const int analysisWidth = (analysisRow.getWidth() - analysisGap) / 2;

  const auto truePeakArea = analysisRow.withWidth(analysisWidth);
  const auto lraArea = analysisRow
                           .withLeft(truePeakArea.getRight() + analysisGap)
                           .withWidth(analysisWidth);

  const auto drawAnalysisCard = [&g, border, secondaryText](juce::Rectangle<int> area,
                                                            const juce::String &label,
                                                            const juce::String &value,
                                                            juce::Colour valueColour)
  {
    g.setColour(juce::Colour::fromRGB(21, 24, 30));
    g.fillRoundedRectangle(area.toFloat(), 8.0f);

    g.setColour(border);
    g.drawRoundedRectangle(area.toFloat(), 8.0f, 1.0f);

    auto textArea = area.reduced(7);

    g.setColour(secondaryText);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawFittedText(label,
                     textArea.removeFromTop(17),
                     juce::Justification::centred,
                     1);

    g.setColour(valueColour);
    g.setFont(juce::FontOptions(19.0f, juce::Font::bold));
    g.drawFittedText(value,
                     textArea,
                     juce::Justification::centred,
                     1);
  };

  const auto truePeakColour = truePeakHoldDb >= -1.0f
                                  ? juce::Colour::fromRGB(244, 93, 93)
                              : truePeakHoldDb >= -3.0f
                                  ? juce::Colour::fromRGB(244, 190, 70)
                                  : juce::Colour::fromRGB(112, 210, 163);

  const auto truePeakText = truePeakHoldDb <= -99.0f
                                ? juce::String("-∞ dBTP")
                                : juce::String(truePeakHoldDb, 1) + " dBTP";

  const auto lraText = loudnessRangeAvailable
                           ? juce::String(loudnessRange, 1) + " LU"
                           : juce::String("MEASURING");

  const auto lraColour = loudnessRange >= 10.0f
                             ? juce::Colour::fromRGB(244, 190, 70)
                             : juce::Colour::fromRGB(112, 210, 163);

  drawAnalysisCard(truePeakArea,
                   "TRUE PEAK",
                   truePeakText,
                   truePeakColour);

  drawAnalysisCard(lraArea,
                   "LOUDNESS RANGE",
                   lraText,
                   lraColour);

  bounds.removeFromTop(10);

  auto loudnessArea = bounds.removeFromTop(122);

  const int cardGap = 10;
  const int cardWidth = (loudnessArea.getWidth() - cardGap) / 2;

  const auto momentaryArea = loudnessArea.withWidth(cardWidth);
  const auto shortTermArea = loudnessArea
                                 .withLeft(momentaryArea.getRight() + cardGap)
                                 .withWidth(cardWidth);

  const auto drawLoudnessCard = [&g, panel, border, primaryText, secondaryText, accent](juce::Rectangle<int> area,
                                                                                        const juce::String &label,
                                                                                        float value,
                                                                                        const juce::String &description)
  {
    g.setColour(juce::Colour::fromRGB(21, 24, 30));
    g.fillRoundedRectangle(area.toFloat(), 8.0f);

    g.setColour(border);
    g.drawRoundedRectangle(area.toFloat(), 8.0f, 1.0f);

    auto card = area.reduced(7);

    g.setColour(secondaryText);
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.drawFittedText(label,
                     card.removeFromTop(19),
                     juce::Justification::centred,
                     1);

    g.setColour(accent);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawFittedText(loudnessText(value),
                     card.removeFromTop(45),
                     juce::Justification::centred,
                     1);

    g.setColour(secondaryText);
    g.setFont(juce::FontOptions(10.0f));
    g.drawFittedText(description,
                     card,
                     juce::Justification::centred,
                     2);
  };

  drawLoudnessCard(momentaryArea,
                   "MOMENTARY",
                   momentaryLufs,
                   "400 ms\nLUFS");

  drawLoudnessCard(shortTermArea,
                   "SHORT-TERM",
                   shortTermLufs,
                   "3 sec\nLUFS");

  bounds.removeFromTop(14);

  auto meterHeader = bounds.removeFromTop(24);

  g.setColour(secondaryText);
  g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
  g.drawFittedText("SAMPLE PEAKS",
                   meterHeader,
                   juce::Justification::centred,
                   1);

  bounds.removeFromTop(5);

  auto footer = bounds.removeFromBottom(36);
  auto meterArea = bounds;

  const int meterGap = 22;
  const int meterWidth = (meterArea.getWidth() - meterGap) / 2;

  const auto leftMeter = meterArea.withWidth(meterWidth);
  const auto rightMeter = meterArea
                              .withLeft(leftMeter.getRight() + meterGap)
                              .withWidth(meterWidth);

  const auto drawPeakMeter =
      [&g](juce::Rectangle<int> area,
           float currentDb,
           float holdDb,
           const juce::String &label)
  {
    constexpr float minimumDb = -60.0f;
    constexpr float maximumDb = 0.0f;

    const auto labelArea = area.removeFromTop(22);
    const auto valueArea = area.removeFromBottom(28);
    const auto barArea = area.reduced(18, 8);

    const auto dbToY = [&barArea, minimumDb, maximumDb](float db)
    {
      const float clampedDb = juce::jlimit(minimumDb, maximumDb, db);

      const float proportion = (clampedDb - minimumDb) / (maximumDb - minimumDb);

      return static_cast<float>(barArea.getBottom()) - proportion * static_cast<float>(barArea.getHeight());
    };

    g.setColour(juce::Colour::fromRGB(190, 196, 206));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawFittedText(label,
                     labelArea,
                     juce::Justification::centred,
                     1);

    g.setColour(juce::Colour::fromRGB(14, 16, 20));
    g.fillRoundedRectangle(barArea.toFloat(), 7.0f);

    // Fixed dim background zones: bottom = green, middle = yellow, top = red.
    const float greenTop = dbToY(-12.0f);
    const float yellowTop = dbToY(-3.0f);

    g.setColour(juce::Colour::fromRGB(87, 214, 139).withAlpha(0.14f));
    g.fillRect(juce::Rectangle<float>(
        static_cast<float>(barArea.getX() + 2),
        greenTop,
        static_cast<float>(barArea.getWidth() - 4),
        static_cast<float>(barArea.getBottom()) - greenTop));

    g.setColour(juce::Colour::fromRGB(244, 190, 70).withAlpha(0.14f));
    g.fillRect(juce::Rectangle<float>(
        static_cast<float>(barArea.getX() + 2),
        yellowTop,
        static_cast<float>(barArea.getWidth() - 4),
        greenTop - yellowTop));

    g.setColour(juce::Colour::fromRGB(244, 93, 93).withAlpha(0.14f));
    g.fillRect(juce::Rectangle<float>(
        static_cast<float>(barArea.getX() + 2),
        static_cast<float>(barArea.getY() + 2),
        static_cast<float>(barArea.getWidth() - 4),
        yellowTop - static_cast<float>(barArea.getY() + 2)));

    // Current live level always begins at the bottom and grows upward.
    const float currentTop = dbToY(currentDb);

    const auto fillBand =
        [&g, &barArea, currentTop](float top,
                                   float bottom,
                                   juce::Colour colour)
    {
      const float visibleTop = juce::jmax(currentTop, top);
      const float visibleBottom = juce::jmin(
          static_cast<float>(barArea.getBottom()),
          bottom);

      if (visibleBottom > visibleTop)
      {
        g.setColour(colour);
        g.fillRect(juce::Rectangle<float>(
            static_cast<float>(barArea.getX() + 2),
            visibleTop,
            static_cast<float>(barArea.getWidth() - 4),
            visibleBottom - visibleTop));
      }
    };

    // Filled zones. These cannot extend below the meter or bounce from the top.
    fillBand(greenTop,
             static_cast<float>(barArea.getBottom()),
             juce::Colour::fromRGB(87, 214, 139));

    fillBand(yellowTop,
             greenTop,
             juce::Colour::fromRGB(244, 190, 70));

    fillBand(static_cast<float>(barArea.getY() + 2),
             yellowTop,
             juce::Colour::fromRGB(244, 93, 93));

    for (int dbMark = 0; dbMark >= -60; dbMark -= 6)
    {
      const float y = dbToY(static_cast<float>(dbMark));

      g.setColour(juce::Colours::white.withAlpha(0.12f));
      g.drawHorizontalLine(static_cast<int>(y),
                           static_cast<float>(barArea.getX()),
                           static_cast<float>(barArea.getRight()));
    }

    // The white hold line uses the held peak, not the current fast peak.
    const float holdY = dbToY(holdDb);

    g.setColour(juce::Colours::white.withAlpha(0.94f));
    g.drawHorizontalLine(static_cast<int>(holdY),
                         static_cast<float>(barArea.getX() + 3),
                         static_cast<float>(barArea.getRight() - 3));

    g.setColour(juce::Colour::fromRGB(72, 80, 93));
    g.drawRoundedRectangle(barArea.toFloat(), 7.0f, 1.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawFittedText(juce::String(holdDb, 1) + " dBFS",
                     valueArea,
                     juce::Justification::centred,
                     1);
  };

  drawPeakMeter(leftMeter, leftPeakDb, leftPeakHoldDb, "LEFT");
  drawPeakMeter(rightMeter, rightPeakDb, rightPeakHoldDb, "RIGHT");

  if (clipDetected)
  {
    const auto clipArea = footer.removeFromTop(18);

    g.setColour(juce::Colour::fromRGB(244, 93, 93));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawFittedText("CLIP DETECTED — RESET PEAKS TO CLEAR",
                     clipArea,
                     juce::Justification::centred,
                     1);
  }

  g.setColour(juce::Colour::fromRGB(244, 190, 70));
  g.setFont(juce::FontOptions(10.5f));
  g.drawFittedText("K-weighted loudness - 4× true peak - EBU-style integrated gating",
                   footer,
                   juce::Justification::centred,
                   1);
}

void LufMeterAudioProcessorEditor::resized()
{
  auto bounds = getLocalBounds().reduced(14);
  bounds.removeFromTop(14);

  bounds.removeFromTop(58);
  bounds.removeFromTop(10);

  auto integratedArea = bounds.removeFromTop(112).reduced(8);

  const int buttonWidth = 96;
  const int buttonHeight = 22;

  resetButton.setBounds(integratedArea.getRight() - buttonWidth,
                        integratedArea.getY() + 4,
                        buttonWidth,
                        buttonHeight);

  auto peakButtonArea = getLocalBounds().reduced(14);
  peakButtonArea.removeFromTop(14);
  peakButtonArea.removeFromTop(58);
  peakButtonArea.removeFromTop(10);
  peakButtonArea.removeFromTop(112);
  peakButtonArea.removeFromTop(10);
  peakButtonArea.removeFromTop(72);
  peakButtonArea.removeFromTop(10);
  peakButtonArea.removeFromTop(122);
  peakButtonArea.removeFromTop(14);
  peakButtonArea.removeFromTop(24);
  peakButtonArea.removeFromTop(5);

  const int peakButtonWidth = 112;
  const int peakButtonHeight = 22;

  resetPeaksButton.setBounds(peakButtonArea.getCentreX() - peakButtonWidth / 2,
                             peakButtonArea.getY() + 24,
                             peakButtonWidth,
                             peakButtonHeight);
}

void LufMeterAudioProcessorEditor::timerCallback()
{
  leftPeakDb = gainToDecibels(audioProcessor.getLeftPeak());
  rightPeakDb = gainToDecibels(audioProcessor.getRightPeak());
  leftPeakHoldDb = gainToDecibels(audioProcessor.getLeftPeakHold());
  rightPeakHoldDb = gainToDecibels(audioProcessor.getRightPeakHold());
  truePeakHoldDb = gainToDecibels(audioProcessor.getTruePeakHold());
  clipDetected = audioProcessor.hasClipped();
  momentaryLufs = audioProcessor.getMomentaryLufs();
  shortTermLufs = audioProcessor.getShortTermLufs();
  integratedLufs = audioProcessor.getIntegratedLufs();
  integratedMeasurementAvailable = audioProcessor.hasIntegratedMeasurement();
  loudnessRange = audioProcessor.getLoudnessRange();
  loudnessRangeAvailable = audioProcessor.hasLoudnessRangeMeasurement();

  repaint();
}

void LufMeterAudioProcessorEditor::resetIntegratedMeasurement()
{
  audioProcessor.requestIntegratedReset();
}

void LufMeterAudioProcessorEditor::resetPeakMeasurement()
{
  audioProcessor.requestPeakReset();
}

float LufMeterAudioProcessorEditor::gainToDecibels(float gain) noexcept
{
  if (gain <= 0.000001f)
    return -100.0f;

  return 20.0f * std::log10(gain);
}

juce::Colour LufMeterAudioProcessorEditor::colourForPeak(float db) noexcept
{
  if (db >= -3.0f)
    return juce::Colour::fromRGB(244, 93, 93);

  if (db >= -12.0f)
    return juce::Colour::fromRGB(244, 190, 70);

  return juce::Colour::fromRGB(87, 214, 139);
}

juce::String LufMeterAudioProcessorEditor::loudnessText(float value)
{
  if (value <= -99.0f)
    return "-∞ LUFS";

  return juce::String(value, 1) + " LUFS";
}