// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/UiTheme.h"
#include "application/EditorPresenter.h"
namespace opentone
{

juce::Colour background(0xffc6c9ce), panel(0xffb8bdc4), accent(juce::Colour(0xffc6c9ce).darker(0.2f)), ink(0xff252a30), line(0xff8c939c), primary(0xff40bd72);


void showTrackCreationMenu(EditorPresenter& presenter, juce::Component& anchor)
{
    juce::PopupMenu menu; menu.addItem(1, "Add audio track"); menu.addItem(2, "Add MIDI track");
    juce::WeakReference<EditorPresenter> owner(&presenter);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor).withMousePosition(), [owner](int choice)
    { if (!owner) return; if (choice == 1) owner->addTrack(); if (choice == 2) owner->addMidiTrack(); });
}
juce::Colour trackColour(const juce::String& id, bool selected)
{
    static constexpr juce::uint32 colours[] = { 0xffe3b543, 0xff67a8dd, 0xff89bf58, 0xffb67fbe,
                                              0xffda806b, 0xff50b6a9, 0xff9690cc, 0xffc6c64c };
    const auto colour = juce::Colour(colours[static_cast<juce::uint32>(id.hashCode()) % 8]);
    return selected ? (background.getPerceivedBrightness() < 0.5f ? colour.brighter(0.1f) : colour.darker(0.08f)) : colour;
}
juce::MouseCursor bracketCursor(bool right)
{
    static const auto cursors = []
    {
        std::array<juce::MouseCursor, 2> result;
        for (size_t side = 0; side < result.size(); ++side)
        {
            juce::Image image(juce::Image::ARGB, 32, 48, true); juce::Graphics graphics(image);
            const float spine = side == 0 ? 10.0f : 22.0f, tip = side == 0 ? 22.0f : 10.0f;
            juce::Path bracket; bracket.startNewSubPath(tip, 6); bracket.lineTo(spine, 6);
            bracket.lineTo(spine, 42); bracket.lineTo(tip, 42);
            graphics.setColour(juce::Colours::white); graphics.strokePath(bracket, juce::PathStrokeType(6));
            graphics.setColour(ink); graphics.strokePath(bracket, juce::PathStrokeType(3));
            result[side] = juce::MouseCursor(image, static_cast<int>(spine / 2), 12, 2);
        }
        return result;
    }();
    return cursors[right ? 1 : 0];
}

namespace
{
class WindowButton final : public juce::Button
{
public:
    explicit WindowButton(int buttonType) : Button(buttonType == juce::DocumentWindow::closeButton ? "Close"
        : buttonType == juce::DocumentWindow::minimiseButton ? "Minimise" : "Maximise"), type(buttonType) {}
    void paintButton(juce::Graphics& graphics, bool hover, bool down) override
    {
        const auto* window = findParentComponentOfClass<juce::DocumentWindow>();
        const bool active = window && window->isActiveWindow();
        const auto colour = type == juce::DocumentWindow::closeButton ? juce::Colour(0xffff5f57)
            : type == juce::DocumentWindow::minimiseButton ? juce::Colour(0xfffebc2e) : juce::Colour(0xff28c840);
        const auto circle = getLocalBounds().toFloat().withSizeKeepingCentre(12, 12);
        graphics.setColour(active || hover ? (down ? colour.darker(0.15f) : colour) : ink.withAlpha(0.3f)); graphics.fillEllipse(circle);
        if (hover || down)
        {
            graphics.setColour(juce::Colours::black.withAlpha(0.6f)); const auto x = circle.getCentreX(), y = circle.getCentreY();
            if (type == juce::DocumentWindow::closeButton)
            { graphics.drawLine(x - 2, y - 2, x + 2, y + 2); graphics.drawLine(x - 2, y + 2, x + 2, y - 2); }
            else { graphics.drawLine(x - 3, y, x + 3, y); if (type == juce::DocumentWindow::maximiseButton) graphics.drawLine(x, y - 3, x, y + 3); }
        }
    }
private: int type;
};
class ArrangementLook final : public juce::LookAndFeel_V4
{
public:
    ArrangementLook()
    {
        configureUiTheme(*this);
    }
    juce::Button* createDocumentWindowButton(int type) override { return new WindowButton(type); }
    void positionDocumentWindowButtons(juce::DocumentWindow&, int x, int y, int width, int height,
                                       juce::Button* minimise, juce::Button* maximise, juce::Button* close, bool left) override
    {
        const int first = left ? x + 8 : x + width - 62;
        int index = 0;
        for (auto* button : { close, minimise, maximise })
        { if (button) button->setBounds(first + index * 18, y + (height - 18) / 2, 14, 18); ++index; }
    }
    void drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& graphics, int width, int height,
                                    int, int, const juce::Image*, bool) override
    {
        graphics.fillAll(panel); graphics.setColour(window.isActiveWindow() ? ink : ink.withAlpha(0.6f));
        graphics.setFont(juce::FontOptions(14));
        graphics.drawText(window.getName(), 68, 0, std::max(0, width - 136), height, juce::Justification::centred, true);
    }
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        auto layout = juce::LookAndFeel_V4::getSliderLayout(slider);
        if (slider.getSliderStyle() == juce::Slider::IncDecButtons)
        {
            const auto side = std::max(1, std::min(slider.getHeight(), (slider.getWidth() - 4) / 2));
            // JUCE trims two pixels at each side before splitting these buttons.
            layout.sliderBounds = {slider.getWidth() - 2 * side - 4, (slider.getHeight() - side) / 2, 2 * side + 4, side};
            layout.textBoxBounds = {0, 0, std::max(0, slider.getWidth() - 2 * side - 4), slider.getHeight()};
        }
        return layout;
    }
    juce::Font getTextButtonFont(juce::TextButton&, int height) override { return juce::Font(juce::FontOptions(height <= 18 ? 10.0f : 12.0f)); }
    juce::Font getLabelFont(juce::Label& label) override { return label.getFont(); }
    void drawMenuBarBackground(juce::Graphics& graphics, int width, int height, bool, juce::MenuBarComponent&) override
    { graphics.fillAll(panel); graphics.setColour(line); graphics.drawHorizontalLine(height - 1, 0, static_cast<float>(width)); }
    void drawButtonBackground(juce::Graphics& graphics, juce::Button& button, const juce::Colour& colour, bool hover, bool down) override
    {
        const bool devicePower = static_cast<bool>(button.getProperties()["device-power"]);
        if (devicePower)
        {
            const auto bounds = button.getLocalBounds().toFloat().reduced(1);
            graphics.setColour(button.getToggleState() ? primary : panel); graphics.fillRoundedRectangle(bounds, 3);
            graphics.setColour(line); graphics.drawRoundedRectangle(bounds, 3, 1); return;
        }
        const auto fill = button.getButtonText() == "Record" ? panel
            : static_cast<bool>(button.getProperties()["track-active"]) ? (button.getToggleState() ? panel : primary) : colour;
        graphics.setColour(down ? fill.darker(0.1f) : (hover ? fill.brighter(0.1f) : fill));
        graphics.fillRect(button.getLocalBounds());
        graphics.setColour(button.hasKeyboardFocus(true) ? ink : line);
        graphics.drawRect(button.getLocalBounds(), button.hasKeyboardFocus(true) ? 2 : 1);
    }
    void drawButtonText(juce::Graphics& graphics, juce::TextButton& button, bool, bool) override
    {
        if (static_cast<bool>(button.getProperties()["device-power"])) return;
        const auto text = button.getButtonText();
        graphics.setColour(static_cast<bool>(button.getProperties()["track-active"]) ? (button.getToggleState() ? ink : primary.contrasting()) : (button.getToggleState() ? primary.contrasting() : ink));
        const auto bounds = button.getLocalBounds().toFloat();
        const auto x = bounds.getCentreX(), y = bounds.getCentreY();
        if (text == "Record")
        { graphics.setColour(button.getToggleState() ? juce::Colour(0xffed514b) : ink.withAlpha(0.6f)); graphics.fillEllipse(x - 4, y - 4, 8, 8); }
        else if (text == "Play")
        { juce::Path triangle; triangle.addTriangle(x - 4, y - 6, x - 4, y + 6, x + 6, y); graphics.fillPath(triangle); }
        else if (text == "Loop")
        { graphics.drawRoundedRectangle(x - 6, y - 4, 12, 8, 2, 1.2f); juce::Path arrow; arrow.addTriangle(x + 1, y - 7, x + 5, y - 4, x + 1, y - 1); graphics.fillPath(arrow); }
        else if (text == "Pause")
        { graphics.fillRect(x - 5, y - 6, 3.0f, 12.0f); graphics.fillRect(x + 2, y - 6, 3.0f, 12.0f); }
        else if (text == "Metronome")
        {
            juce::Path shape; shape.startNewSubPath(x - 5, y + 5); shape.lineTo(x - 2, y - 5);
            shape.lineTo(x + 2, y - 5); shape.lineTo(x + 5, y + 5); shape.closeSubPath();
            graphics.strokePath(shape, juce::PathStrokeType(1)); graphics.drawLine(x, y + 3, x + 4, y - 5, 1.2f);
        }
        else if (text == "Stop") graphics.fillRect(x - 5, y - 5, 10.0f, 10.0f);
        else { graphics.setFont(getTextButtonFont(button, button.getHeight())); graphics.drawText(text, button.getLocalBounds().reduced(3), juce::Justification::centred, true); }
    }
    void drawToggleButton(juce::Graphics& graphics, juce::ToggleButton& button, bool, bool) override
    {
        graphics.setFont(juce::FontOptions(12));
        if (button.getButtonText().containsOnly("0123456789"))
        {
            graphics.setColour(button.getToggleState() ? panel : primary); graphics.fillRect(button.getLocalBounds());
            graphics.setColour(ink); graphics.drawRect(button.getLocalBounds());
            graphics.setColour(button.getToggleState() ? ink : primary.contrasting());
            graphics.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
        }
        else
        {
            const juce::Rectangle<int> box(2, (button.getHeight() - 12) / 2, 12, 12);
            graphics.setColour(button.getToggleState() ? accent : panel); graphics.fillRect(box);
            graphics.setColour(ink); graphics.drawRect(box);
            if (button.getToggleState()) graphics.fillRect(box.reduced(3));
            graphics.drawText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(19), juce::Justification::centredLeft);
        }
        if (button.hasKeyboardFocus(true)) { graphics.setColour(ink); graphics.drawRect(button.getLocalBounds(), 2); }
    }
    void drawLinearSlider(juce::Graphics& graphics, int x, int y, int width, int height, float position, float, float,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearVertical)
        {
            const auto centre = static_cast<float>(x) + static_cast<float>(width) / 2;
            graphics.setColour(line); graphics.fillRect(centre - 2, static_cast<float>(y), 4.0f, static_cast<float>(height));
            graphics.setColour(ink.withAlpha(0.65f)); graphics.fillRect(centre - 2, position, 4.0f, std::max(0.0f, static_cast<float>(y + height) - position));
            graphics.setColour(panel); graphics.fillRect(centre - 10, position - 5, 20.0f, 10.0f);
            graphics.setColour(ink); graphics.drawRect(centre - 10, position - 5, 20.0f, 10.0f, 1.0f);
            if (slider.hasKeyboardFocus(true)) graphics.drawRect(x, y, width, height);
            return;
        }
        const auto centre = static_cast<float>(y) + static_cast<float>(height) / 2;
        graphics.setColour(line); graphics.fillRect(static_cast<float>(x), centre - 2, static_cast<float>(width), 4.0f);
        graphics.setColour(ink.withAlpha(0.65f)); graphics.fillRect(static_cast<float>(x), centre - 2, std::max(0.0f, position - static_cast<float>(x)), 4.0f);
        graphics.setColour(ink); graphics.fillRect(position - 2, centre - 7, 4.0f, 14.0f);
        if (slider.hasKeyboardFocus(true)) graphics.drawRect(x, y, width, height);
    }
};
}

void label(juce::Label& value, const juce::String& text)
{ value.setText(text, juce::dontSendNotification); value.setFont(juce::FontOptions(12)); value.setColour(juce::Label::textColourId, ink); }
juce::String volumeText(double gain)
{ return gain <= 0 ? juce::String("-inf dB") : juce::String(20 * std::log10(gain), 1) + " dB"; }
void setSliderDefault(juce::Slider& slider, double value)
{
    struct Reset final : juce::MouseListener
    {
        void mouseDoubleClick(const juce::MouseEvent& event) override
        {
            auto* component = event.originalComponent;
            while (component && dynamic_cast<juce::Slider*>(component) == nullptr) component = component->getParentComponent();
            auto* control = dynamic_cast<juce::Slider*>(component); if (!control || !control->isDoubleClickReturnEnabled()) return;
            auto* label = dynamic_cast<juce::Label*>(event.originalComponent);
            if (!label) label = event.originalComponent->findParentComponentOfClass<juce::Label>();
            if (label) label->hideEditor(true);
            control->setValue(control->getDoubleClickReturnValue(), juce::sendNotificationSync);
        }
    };
    static Reset reset;
    slider.setDoubleClickReturnValue(true, value); slider.addMouseListener(&reset, true);
}
void configurePanSlider(juce::Slider& slider)
{
    slider.setRange(-1, 1, 0.01); slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16); setSliderDefault(slider, 0);
    slider.setTitle("Pan; double click to center");
    slider.textFromValueFunction = [](double value) { return std::abs(value) < 0.005 ? juce::String("C") : juce::String(std::abs(value) * 50, 0) + (value < 0 ? " L" : " R"); };
    slider.valueFromTextFunction = [](const juce::String& text) { return text.containsIgnoreCase("C") ? 0.0 : juce::jlimit(-1.0, 1.0, text.containsIgnoreCase("L") ? -std::abs(text.getDoubleValue()) / 50 : text.getDoubleValue() / 50); };
}
juce::String assetName(const Asset& asset)
{
    const auto filename = asset.path.fromLastOccurrenceOf("/", false, false);
    return filename.startsWith(asset.id + "-") ? filename.substring(asset.id.length() + 1) : filename;
}
juce::Path waveformEnvelope(juce::AudioThumbnail& thumbnail, juce::Rectangle<float> area, double from, double to)
{
    juce::Path path; const int width = juce::roundToInt(area.getWidth());
    if (width < 1 || to <= from || thumbnail.getNumChannels() < 1) return path;
    std::vector<float> bottom; bottom.reserve(static_cast<size_t>(width));
    const auto centre = area.getCentreY(), scale = area.getHeight() * 0.46f;
    for (int pixel = 0; pixel < width; ++pixel)
    {
        float low = 0, high = 0;
        const auto begin = from + (to - from) * pixel / width, end = from + (to - from) * (pixel + 1) / width;
        for (int channel = 0; channel < thumbnail.getNumChannels(); ++channel)
        { float minimum = 0, maximum = 0; thumbnail.getApproximateMinMax(begin, end, channel, minimum, maximum);
          low = std::min(low, minimum); high = std::max(high, maximum); }
        const auto x = area.getX() + static_cast<float>(pixel), y = centre - juce::jlimit(0.0f, 1.0f, high) * scale;
        if (pixel == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        bottom.push_back(centre - juce::jlimit(-1.0f, 0.0f, low) * scale);
    }
    for (int pixel = width; pixel-- > 0;) path.lineTo(area.getX() + static_cast<float>(pixel), bottom[static_cast<size_t>(pixel)]);
    path.closeSubPath(); return path;
}
namespace
{
juce::File appearanceFile()
{ return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Opentone/appearance.json"); }
}
namespace
{
const std::array<juce::uint32, 6> lightColours { 0xffc6c9ce, 0xffb8bdc4, juce::Colour(0xffc6c9ce).darker(0.2f).getARGB(), 0xff252a30, 0xff8c939c, 0xff40bd72 };
const std::array<juce::uint32, 6> darkColours { 0xff25282d, 0xff30343b, juce::Colour(0xff25282d).brighter(0.3f).getARGB(), 0xffe3e6eb, 0xff59616b, 0xff40bd72 };
}
void setUiThemePreset(bool dark)
{
    const auto& colours = dark ? darkColours : lightColours;
    background = juce::Colour(colours[0]); panel = juce::Colour(colours[1]); accent = juce::Colour(colours[2]);
    ink = juce::Colour(colours[3]); line = juce::Colour(colours[4]); primary = juce::Colour(colours[5]);
}
int uiThemePreset()
{
    const std::array<juce::uint32, 6> colours { background.getARGB(), panel.getARGB(), accent.getARGB(), ink.getARGB(), line.getARGB(), primary.getARGB() };
    return colours == lightColours ? 1 : colours == darkColours ? 2 : 3;
}
void resetUiColours() { setUiThemePreset(false); }
void loadUiColours()
{
    const auto value = juce::JSON::parse(appearanceFile()); if (!value.isObject()) return;
    for (auto entry : { std::pair<const char*, juce::Colour*> {"background", &background}, {"panel", &panel}, {"accent", &accent}, {"ink", &ink}, {"line", &line}, {"primary", &primary} })
        if (value[entry.first].isString() && value[entry.first].toString().containsOnly("0123456789abcdefABCDEF") && value[entry.first].toString().length() == 8)
            *entry.second = juce::Colour::fromString(value[entry.first].toString()).withAlpha(1.0f);
    if (static_cast<int>(value["version"]) < 2 && accent.getARGB() == 0xffffc52d)
        accent = background.getPerceivedBrightness() < 0.5f ? background.brighter(0.3f) : background.darker(0.2f);
}
void saveUiColours()
{
    auto* object = new juce::DynamicObject(); juce::var value(object); object->setProperty("version", 2);
    for (auto entry : { std::pair<const char*, juce::Colour*> {"background", &background}, {"panel", &panel}, {"accent", &accent}, {"ink", &ink}, {"line", &line}, {"primary", &primary} })
        object->setProperty(entry.first, entry.second->toString());
    auto file = appearanceFile(); file.getParentDirectory().createDirectory(); file.replaceWithText(juce::JSON::toString(value));
}
void configureUiTheme(juce::LookAndFeel_V4& theme)
{
        theme.setColourScheme({ panel, background, panel, line, ink, line, ink, accent, ink });
        theme.setColour(juce::TextButton::buttonColourId, panel);
        theme.setColour(juce::TextButton::buttonOnColourId, primary);
        theme.setColour(juce::TextButton::textColourOffId, ink);
        theme.setColour(juce::TextButton::textColourOnId, primary.contrasting());
        theme.setColour(juce::Label::textColourId, ink);
        theme.setColour(juce::ListBox::backgroundColourId, background);
        theme.setColour(juce::ListBox::textColourId, ink);
        theme.setColour(juce::TreeView::backgroundColourId, background);
        theme.setColour(juce::TreeView::linesColourId, line);
        theme.setColour(juce::DirectoryContentsDisplayComponent::textColourId, ink);
        theme.setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, accent.withAlpha(0.3f));
        theme.setColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId, ink);
        theme.setColour(juce::ListBox::outlineColourId, line);
        theme.setColour(juce::TextEditor::backgroundColourId, background.brighter(0.08f));
        theme.setColour(juce::TextEditor::textColourId, ink);
        theme.setColour(juce::TextEditor::outlineColourId, line);
        theme.setColour(juce::TextEditor::focusedOutlineColourId, ink);
        theme.setColour(juce::CaretComponent::caretColourId, ink);
        theme.setColour(juce::Slider::textBoxTextColourId, ink);
        theme.setColour(juce::Slider::textBoxBackgroundColourId, background.brighter(0.08f));
        theme.setColour(juce::Slider::textBoxOutlineColourId, line);
        theme.setColour(juce::PopupMenu::backgroundColourId, background.brighter(0.08f));
        theme.setColour(juce::PopupMenu::textColourId, ink);
        theme.setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
        theme.setColour(juce::PopupMenu::highlightedTextColourId, accent.contrasting());
        theme.setColour(juce::ScrollBar::thumbColourId, line);
}
std::unique_ptr<juce::LookAndFeel_V4> createUiTheme() { return std::make_unique<ArrangementLook>(); }
}
