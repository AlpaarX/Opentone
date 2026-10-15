// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/UiTheme.h"
#include "application/EditorPresenter.h"
#include "domain/TimelineMath.h"
namespace opentone
{

juce::Colour background(0xff363636), panel(0xff2a2a2a), accent(0xffb0ddeb), ink(0xffb5b5b5), line(0xff111111), primary(0xffffad56);

juce::Colour uiTone(UiTone tone)
{
    // Live 12's Default Dark/Light Neutral Medium theme colour values.
    static constexpr std::array<juce::uint32, 11> dark { 0xff1e1e1e, 0xff181818, 0xff03c3d5, 0xff070707, 0xff464646, 0xff868686, 0xff3e3e3e, 0xffff5559, 0xff3c6ab6, 0xff00d38d, 0xff007383 };
    static constexpr std::array<juce::uint32, 11> light { 0xffcfcfcf, 0xff5b5b5b, 0xff00eeff, 0xff121212, 0xff5a5a5a, 0xfff1f1f1, 0xffc6c6c6, 0xffff595f, 0xff1a7df1, 0xff00faa3, 0xff00cadb };
    const auto index = static_cast<size_t>(tone); jassert(index < dark.size());
    return juce::Colour((background.getPerceivedBrightness() < 0.5f ? dark : light)[index]);
}


void showTrackCreationMenu(EditorPresenter& presenter, juce::Component& anchor)
{
    juce::PopupMenu menu; menu.addItem(1, "Add audio track"); menu.addItem(2, "Add MIDI track"); menu.addItem(3, "Add group track");
    juce::WeakReference<EditorPresenter> owner(&presenter);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor).withMousePosition(), [owner](int choice)
    { if (!owner) return; if (choice == 1) owner->addTrack(); if (choice == 2) owner->addMidiTrack(); if (choice == 3) owner->createGroup(); });
}
juce::Colour trackColour(const Project& project, const juce::String& id, bool selected)
{
    static constexpr juce::uint32 colours[] = { 0xff8b7936, 0xff999565, 0xffb8ce93, 0xffafb95b,
                                              0xff52ba46, 0xff81d24c, 0xff6baace, 0xff4881aa,
                                              0xff954eb2, 0xffff5f80, 0xffdc4848, 0xffd66b18,
                                              0xffe0aa2a, 0xffffec75, 0xffe7e6e6, 0xffa0a0a0 };
    const auto* track = findTrack(project, id);
    const auto colour = track && track->color >= 0 ? juce::Colour(0xff000000u | static_cast<juce::uint32>(track->color)) : id == "master" ? juce::Colour(0xff45bf00) : juce::Colour(colours[static_cast<juce::uint32>(id.hashCode()) % 16]);
    return selected ? accent : colour;
}
juce::Colour clipColour(const Project& project, const Clip& clip)
{ return clip.color < 0 ? trackColour(project, clip.track) : juce::Colour(0xff000000u | static_cast<juce::uint32>(clip.color)); }
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
    juce::Font getTextButtonFont(juce::TextButton&, int height) override { return juce::Font(juce::FontOptions(height <= 20 ? 10.0f : 11.0f)); }
    juce::Font getLabelFont(juce::Label& label) override { return label.getFont(); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(juce::FontOptions(10)); }
    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* value = juce::LookAndFeel_V4::createSliderTextBox(slider); value->setFont(juce::FontOptions(10));
        const auto text = static_cast<bool>(slider.getProperties()["mixer-pan"]) || static_cast<bool>(slider.getProperties()["mixer-volume"]) || static_cast<bool>(slider.getProperties()["mixer-send"]) ? ink
            : slider.findColour(juce::Slider::textBoxTextColourId);
        value->setColour(juce::Label::textColourId, text); value->setColour(juce::TextEditor::textColourId, text);
        if (slider.isBar()) value->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return value;
    }
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&) override
    {
        g.setColour(uiTone(UiTone::control)); g.fillRect(0, 0, width, height); g.setColour(line); g.drawRect(0, 0, width, height);
        const auto x = static_cast<float>(width - 7), y = static_cast<float>(height) * 0.5f;
        g.setColour(ink); juce::Path arrow; arrow.addTriangle(x - 3, y - 2, x + 3, y - 2, x, y + 2); g.fillPath(arrow);
    }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& value) override
    { value.setBounds(3, 0, std::max(0, box.getWidth() - 15), box.getHeight()); value.setFont(getComboBoxFont(box)); }
    void drawMenuBarBackground(juce::Graphics& graphics, int width, int height, bool, juce::MenuBarComponent&) override
    { graphics.fillAll(panel); graphics.setColour(line); graphics.drawHorizontalLine(height - 1, 0, static_cast<float>(width)); }
    void drawButtonBackground(juce::Graphics& graphics, juce::Button& button, const juce::Colour& colour, bool hover, bool down) override
    {
        if (static_cast<bool>(button.getProperties()["map-mode"]))
        { graphics.setColour(button.getToggleState() ? uiTone(UiTone::solo) : uiTone(UiTone::control)); graphics.fillRect(button.getLocalBounds()); return; }
        if (button.getProperties()["footer-icon"].isString())
        {
            if (hover || down || button.getToggleState())
            { graphics.setColour(button.getToggleState() ? primary : background); graphics.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(1), 4); }
            return;
        }
        if (static_cast<bool>(button.getProperties()["footer-track"]))
        { graphics.setColour(background); graphics.fillRoundedRectangle(button.getLocalBounds().toFloat(), 3); return; }
        if (static_cast<bool>(button.getProperties()["track-fold"]))
        { if (hover || down) { graphics.setColour(accent.withAlpha(0.3f)); graphics.fillEllipse(button.getLocalBounds().toFloat()); } return; }
        if (static_cast<bool>(button.getProperties()["transport-icon"]))
        { graphics.setColour(button.getToggleState() ? (button.getButtonText() == "Play" || button.getButtonText() == "Pause" ? uiTone(UiTone::play) : primary) : (hover || down ? background : panel)); graphics.fillRect(button.getLocalBounds()); return; }
        const bool devicePower = static_cast<bool>(button.getProperties()["device-power"]);
        if (devicePower)
        {
            const auto bounds = button.getLocalBounds().toFloat().reduced(1);
            graphics.setColour(button.getToggleState() ? primary : panel); graphics.fillRoundedRectangle(bounds, 3);
            graphics.setColour(line); graphics.drawRoundedRectangle(bounds, 3, 1); return;
        }
        const auto fill = static_cast<bool>(button.getProperties()["track-arm"]) ? (button.getToggleState() ? uiTone(UiTone::record) : panel)
            : static_cast<bool>(button.getProperties()["track-solo"]) ? (button.getToggleState() ? uiTone(UiTone::solo) : panel)
            : button.getButtonText() == "Record" ? panel
            : static_cast<bool>(button.getProperties()["track-active"]) ? (button.getToggleState() || static_cast<bool>(button.getProperties()["group-muted"]) ? panel : primary) : colour;
        graphics.setColour(down ? fill.darker(0.1f) : (hover ? fill.brighter(0.1f) : fill));
        graphics.fillRect(button.getLocalBounds());
        if (static_cast<bool>(button.getProperties()["group-solo-child"]) && !button.getToggleState())
        { graphics.setColour(uiTone(UiTone::solo)); graphics.fillRect(button.getLocalBounds().withWidth(button.getWidth() / 2)); }
        graphics.setColour(button.hasKeyboardFocus(true) ? ink : line);
        graphics.drawRect(button.getLocalBounds(), button.hasKeyboardFocus(true) ? 2 : 1);
    }
    void drawButtonText(juce::Graphics& graphics, juce::TextButton& button, bool, bool) override
    {
        if (button.getProperties()["footer-icon"].isString())
        {
            const auto role = button.getProperties()["footer-icon"].toString(); const auto centre = button.getLocalBounds().toFloat().getCentre();
            const auto x = centre.x, y = centre.y; graphics.setColour(button.getToggleState() ? primary.contrasting() : ink);
            if (role == "info")
            { graphics.drawEllipse(x - 6, y - 6, 12, 12, 1); graphics.fillEllipse(x - 0.75f, y - 4, 1.5f, 1.5f); graphics.drawLine(x, y - 1, x, y + 4, 1.5f); }
            else if (role == "detail")
            { juce::Path arrow; if (button.getToggleState()) arrow.addTriangle(x - 4, y - 2, x + 4, y - 2, x, y + 3);
              else arrow.addTriangle(x - 4, y + 2, x + 4, y + 2, x, y - 3); graphics.fillPath(arrow); }
            else if (role == "follow")
            { graphics.drawHorizontalLine(juce::roundToInt(y), x - 6, x + 5); juce::Path arrow; arrow.addTriangle(x + 1, y - 3, x + 1, y + 3, x + 6, y); graphics.fillPath(arrow); graphics.fillRect(x - 5, y - 2, 2.0f, 4.0f); }
            else if (role == "overdub") { graphics.drawLine(x - 5, y, x + 5, y, 1.5f); graphics.drawLine(x, y - 5, x, y + 5, 1.5f); }
            else if (role == "punch-in" || role == "punch-out")
            { const auto sign = role == "punch-in" ? 1.0f : -1.0f; graphics.drawLine(x - 6 * sign, y - 4, x - 2 * sign, y - 4, 1); graphics.drawLine(x - 2 * sign, y - 4, x + 2 * sign, y + 4, 1); graphics.drawLine(x + 2 * sign, y + 4, x + 6 * sign, y + 4, 1); }
            else if (role == "keyboard")
            { graphics.drawRect(x - 6, y - 5, 12.0f, 10.0f, 1.0f); for (int i = 1; i < 4; ++i) graphics.drawLine(x - 6 + i * 3, y - 5, x - 6 + i * 3, y + 5, 1); }
            else if (role == "draw") { graphics.drawLine(x - 4, y + 4, x + 4, y - 4, 3); graphics.drawLine(x - 6, y + 6, x - 3, y + 4, 1); }
            else if (role == "scale") { graphics.drawVerticalLine(juce::roundToInt(x - 1), y - 6, y + 3); graphics.fillEllipse(x - 5, y + 1, 4, 3); graphics.drawLine(x - 1, y - 6, x + 5, y - 4, 2); }
            else if (role == "browser") { graphics.drawRect(x - 6, y - 5, 12.0f, 10.0f, 1.0f); graphics.fillRect(x - 6, y - 5, 4.0f, 10.0f); }
            else if (role == "lock") { graphics.drawRoundedRectangle(x - 4, y - 1, 8, 7, 1, 1); graphics.drawRoundedRectangle(x - 3, y - 6, 6, 7, 3, 1); }
            else if (role == "automation")
            { graphics.drawLine(x - 6, y + 4, x + 5, y - 4, 1); graphics.fillEllipse(x - 5, y + 1, 3, 3); graphics.fillEllipse(x + 1, y - 4, 3, 3); }
            else if (role == "previous" || role == "next")
            { const auto sign = role == "next" ? 1.0f : -1.0f; juce::Path arrow; arrow.addTriangle(x - 3 * sign, y - 4, x - 3 * sign, y + 4, x + 3 * sign, y); graphics.fillPath(arrow); }
            else if (role == "mixer")
            { for (int i = 0; i < 3; ++i) { const auto left = x - 5 + static_cast<float>(i) * 5;
                graphics.drawVerticalLine(juce::roundToInt(left), y - 6, y + 6); graphics.fillRect(left - 1.5f, y - 3 + static_cast<float>(i) * 2, 3.0f, 2.0f); } }
            else if (role == "clip")
            { graphics.drawRect(x - 6, y - 5, 12.0f, 10.0f, 1.0f); graphics.fillRect(x - 4, y - 3, 5.0f, 2.0f); graphics.fillRect(x - 1, y + 1, 5.0f, 2.0f); }
            else
            { graphics.drawHorizontalLine(juce::roundToInt(y), x - 6, x + 6); for (int i = 0; i < 3; ++i) graphics.drawRect(x - 6 + static_cast<float>(i) * 5, y - 4, 3.0f, 8.0f, 1.0f); }
            return;
        }
        if (static_cast<bool>(button.getProperties()["footer-track"]))
        {
            const auto colour = juce::Colour(static_cast<juce::uint32>(static_cast<int>(button.getProperties()["footer-track-colour"])));
            const auto box = button.getLocalBounds().toFloat().removeFromLeft(15).reduced(1);
            graphics.setColour(colour); graphics.fillRoundedRectangle(box, 2);
            graphics.setColour(colour.contrasting()); juce::Path arrow; arrow.addTriangle(box.getX() + 4, box.getY() + 4, box.getX() + 4, box.getBottom() - 4, box.getRight() - 3, box.getCentreY()); graphics.fillPath(arrow);
            graphics.setColour(ink); graphics.setFont(juce::FontOptions(10)); graphics.drawText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(19), juce::Justification::centredLeft, true); return;
        }
        if (static_cast<bool>(button.getProperties()["device-power"])) return;
        if (static_cast<bool>(button.getProperties()["track-fold"]))
        {
            const auto bounds = button.getLocalBounds().toFloat().reduced(1); const auto centre = bounds.getCentre();
            graphics.setColour(juce::Colours::black.withAlpha(0.65f)); graphics.drawEllipse(bounds, 1);
            juce::Path arrow;
            if (button.getToggleState()) arrow.addTriangle(centre.x - 3, centre.y - 1, centre.x + 3, centre.y - 1, centre.x, centre.y + 3);
            else arrow.addTriangle(centre.x - 1, centre.y - 3, centre.x - 1, centre.y + 3, centre.x + 3, centre.y);
            graphics.fillPath(arrow); return;
        }
        const auto text = button.getButtonText();
        graphics.setColour(static_cast<bool>(button.getProperties()["track-active"]) ? (button.getToggleState() ? ink : primary.contrasting()) : (button.getToggleState() ? primary.contrasting() : ink));
        const auto bounds = button.getLocalBounds().toFloat();
        const auto x = bounds.getCentreX(), y = bounds.getCentreY();
        if (text == "Record")
        { graphics.setColour(button.getToggleState() && static_cast<bool>(button.getProperties()["track-arm"]) ? juce::Colours::black : button.getToggleState() ? uiTone(UiTone::record) : ink.withAlpha(0.6f)); graphics.fillEllipse(x - 4, y - 4, 8, 8); }
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
        if (button.getButtonText().containsOnly("0123456789") || static_cast<bool>(button.getProperties()["track-active"]))
        {
            const bool off = button.getToggleState() || static_cast<bool>(button.getProperties()["group-muted"]);
            graphics.setColour(off ? panel : primary); graphics.fillRect(button.getLocalBounds());
            graphics.setColour(ink); graphics.drawRect(button.getLocalBounds());
            graphics.setColour(off ? ink : primary.contrasting());
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
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float value,
                          float start, float end, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)).reduced(3);
        const auto radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre(); const auto angle = start + value * (end - start);
        juce::Path ring; ring.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
        g.setColour(line); g.strokePath(ring, juce::PathStrokeType(3));
        juce::Path active; active.addCentredArc(centre.x, centre.y, radius, radius, 0, start, angle, true);
        g.setColour(uiTone(UiTone::range)); g.strokePath(active, juce::PathStrokeType(3));
        g.setColour(ink); g.drawLine(centre.x, centre.y, centre.x + std::sin(angle) * radius * 0.8f, centre.y - std::cos(angle) * radius * 0.8f, 1.5f);
    }
    int getDefaultScrollbarWidth() override { return 8; }
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                       bool vertical, int start, int size, bool hover, bool) override
    {
        g.setColour(ink.withAlpha(hover ? 0.5f : 0.25f));
        if (vertical) g.fillRoundedRectangle(static_cast<float>(x + 2), static_cast<float>(start), static_cast<float>(width - 4), static_cast<float>(size), 2);
        else g.fillRoundedRectangle(static_cast<float>(start), static_cast<float>(y + 2), static_cast<float>(size), static_cast<float>(height - 4), 2);
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float position, float minimum, float maximum,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
        {
            const bool volume = static_cast<bool>(slider.getProperties()["mixer-volume"]) || static_cast<bool>(slider.getProperties()["mixer-send"]);
            g.setColour(uiTone(UiTone::control)); g.fillRect(x, y, width, height);
            const bool pan = static_cast<bool>(slider.getProperties()["mixer-pan"]);
            if (volume || pan)
            {
                const auto valueX = static_cast<float>(slider.valueToProportionOfLength(slider.getValue())) * static_cast<float>(width);
                const auto origin = pan ? static_cast<float>(width) * 0.5f : 0.0f;
                g.setColour(uiTone(static_cast<bool>(slider.getProperties()["master-volume"]) ? UiTone::solo : UiTone::editField));
                g.fillRect(static_cast<float>(x) + std::min(origin, valueX), static_cast<float>(y), std::abs(valueX - origin), static_cast<float>(height));
            }
            g.setColour(line); g.drawRect(x, y, width, height); return;
        }
        if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
        { juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, position, minimum, maximum, style, slider); return; }
        const bool vertical = style == juce::Slider::LinearVertical;
        const auto centre = vertical ? static_cast<float>(x) + static_cast<float>(width) * 0.5f : static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const auto begin = static_cast<float>(vertical ? y : x), length = static_cast<float>(vertical ? height : width);
        const auto bipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;
        const auto origin = bipolar ? begin + static_cast<float>(slider.valueToProportionOfLength(0)) * length : (vertical ? begin + length : begin);
        const auto fillFrom = std::min(origin, position), fillLength = std::abs(position - origin);
        g.setColour(line);
        if (vertical) g.fillRect(centre - 1, begin, 2.0f, length);
        else g.fillRect(begin, centre - 1, length, 2.0f);
        g.setColour(uiTone(UiTone::range).withAlpha(slider.isEnabled() ? 0.8f : 0.3f));
        if (vertical) g.fillRect(centre - 1, fillFrom, 2.0f, fillLength);
        else g.fillRect(fillFrom, centre - 1, fillLength, 2.0f);
        g.setColour(ink.withAlpha(0.35f));
        if (vertical)
        {
            for (int tick = 0; tick <= 4; ++tick)
                g.drawHorizontalLine(juce::roundToInt(begin + tick * length / 4), centre - 6, centre - 3);
            if (juce::approximatelyEqual(slider.getMinimum(), 0.0) && juce::approximatelyEqual(slider.getMaximum(), 2.0))
                g.drawHorizontalLine(juce::roundToInt(begin + (1.0 - slider.valueToProportionOfLength(1)) * length), centre - 7, centre + 1);
        }
        else if (bipolar) g.drawVerticalLine(juce::roundToInt(origin), centre - 4, centre + 4);
        juce::Path thumb;
        if (vertical) thumb.addTriangle(centre + 1, position, centre + 7, position - 4, centre + 7, position + 4);
        else thumb.addTriangle(position, centre + 1, position - 4, centre + 7, position + 4, centre + 7);
        g.setColour(ink.withAlpha(slider.isEnabled() ? 1.0f : 0.35f)); g.fillPath(thumb);
        if (slider.hasKeyboardFocus(true)) { g.setColour(accent); g.drawRect(x, y, width, height); }
    }
};
}

void drawArrangementGrid(juce::Graphics& g, juce::Rectangle<int> area, const Project& project, double zoom, int scrollX)
{
    if (area.isEmpty() || zoom <= 0) return;
    const auto beat = 60.0 / project.bpm * 4.0 / project.denominator;
    const auto bar = beat * project.numerator;
    auto shade = bar;
    while (shade * zoom > 120) shade /= 2;
    while (shade * zoom < 48) shade *= 2;
    const auto shadeWidth = shade * zoom;
    juce::Graphics::ScopedSaveState saved(g); g.reduceClipRegion(area);
    g.setColour(background.getPerceivedBrightness() < 0.5f ? background.darker(0.08f) : background.brighter(0.08f));
    for (auto i = static_cast<int>(std::floor((area.getX() + scrollX) / shadeWidth)); i * shadeWidth < area.getRight() + scrollX; ++i)
        if (i % 2 != 0) g.fillRect(static_cast<float>(i * shadeWidth - scrollX), static_cast<float>(area.getY()), static_cast<float>(shadeWidth), static_cast<float>(area.getHeight()));
    const auto step = timelineGridStep(project.bpm, zoom), spacing = step * zoom;
    for (auto i = static_cast<int>(std::floor((area.getX() + scrollX) / spacing)); i * spacing < area.getRight() + scrollX; ++i)
    {
        const bool major = std::abs(std::remainder(i * step, bar)) < step * 0.01;
        g.setColour(background.getPerceivedBrightness() < 0.5f ? background.darker(major ? 0.5f : 0.3f) : background.brighter(major ? 0.5f : 0.3f));
        g.drawVerticalLine(juce::roundToInt(i * spacing - scrollX), static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    }
}
void label(juce::Label& value, const juce::String& text)
{ value.setText(text, juce::dontSendNotification); value.setFont(juce::FontOptions(11)); value.setColour(juce::Label::textColourId, ink); }
juce::String volumeText(double gain)
{ return gain <= 0 ? juce::String::fromUTF8("−∞ dB") : (std::abs(20 * std::log10(gain)) < 0.05 ? juce::String("0") : juce::String(20 * std::log10(gain), 1)) + " dB"; }
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
void SendSlider::bind(EditorPresenter& owner, juce::String id, int target, int height)
{
    presenter = &owner; track = std::move(id); index = target;
    getProperties().set("mixer-send", true); setRange(0, maxSendGain); setSkewFactor(0.3);
    setSliderStyle(juce::Slider::LinearBarVertical); setSliderSnapsToMousePosition(false); setScrollWheelEnabled(false);
    setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, height); setSliderDefault(*this, 0);
    textFromValueFunction = [](double value) { return value <= 0 ? juce::String::fromUTF8("−∞") : std::abs(20 * std::log10(value)) < 0.05 ? juce::String("0") : juce::String(20 * std::log10(value), 1); };
    valueFromTextFunction = [](const juce::String& text) { return text.containsIgnoreCase("inf") || text.containsChar(0x221e) ? 0.0 : juce::jlimit(0.0, maxSendGain, std::pow(10.0, text.getDoubleValue() / 20)); };
    onValueChange = [this] { if (presenter) presenter->setSend(track, index, getValue()); };
    onDragStart = [this] { if (presenter) presenter->beginGesture(); }; onDragEnd = [this] { if (presenter) presenter->endGesture(false); };
    getProperties().set("midi-target", "track/" + track + "/send" + juce::String(index));
    updateText(); refresh();
}
void SendSlider::refresh()
{
    if (!presenter) return; const auto& project = presenter->project(); const auto* sender = findTrack(project, track);
    const auto target = std::find_if(project.tracks.begin(), project.tracks.end(), [this](const auto& item) { return item.returnIndex == index; });
    setEnabled(sender && returnCount(project) > 0); setTitle(target != project.tracks.end() ? "Send to " + target->name + "; right click to choose return" : "Return channel is unavailable");
    const auto* focus = juce::Component::getCurrentlyFocusedComponent(); const auto typing = dynamic_cast<const juce::TextEditor*>(focus) != nullptr && isParentOf(focus);
    if (sender && !isMouseButtonDown() && !typing) setValue(presenter->trackAutomationValue(track, index + 3), juce::dontSendNotification);
}
void SendSlider::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu() || !presenter) { juce::Slider::mouseDown(event); return; }
    juce::PopupMenu menu; for (const auto& item : presenter->project().tracks) if (item.returnIndex >= 0) menu.addItem(item.returnIndex + 1, item.name, true, item.returnIndex == index);
    juce::Component::SafePointer<SendSlider> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int choice) { if (safe && choice > 0) { safe->index = choice - 1; safe->getProperties().set("midi-target", "track/" + safe->track + "/send" + juce::String(safe->index)); safe->refresh(); } });
}
void configurePanSlider(juce::Slider& slider)
{
    slider.getProperties().set("mixer-pan", true); slider.setColour(juce::Slider::textBoxTextColourId, ink);
    slider.setRange(-1, 1, 0.01); slider.setSliderStyle(juce::Slider::LinearBarVertical); slider.setSliderSnapsToMousePosition(false);
    slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 62, 16); setSliderDefault(slider, 0);
    slider.setTitle("Pan; double click to center");
    slider.textFromValueFunction = [](double value) { return std::abs(value) < 0.005 ? juce::String("C") : juce::String(std::abs(value) * 50, 0) + (value < 0 ? " L" : " R"); };
    slider.valueFromTextFunction = [](const juce::String& text) { return text.containsIgnoreCase("C") ? 0.0 : juce::jlimit(-1.0, 1.0, text.containsIgnoreCase("L") ? -std::abs(text.getDoubleValue()) / 50 : text.getDoubleValue() / 50); };
    slider.updateText();
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
    const auto channelHeight = area.getHeight() / static_cast<float>(thumbnail.getNumChannels());
    std::vector<float> bottom; bottom.reserve(static_cast<size_t>(width));
    for (int channel = 0; channel < thumbnail.getNumChannels(); ++channel)
    {
        bottom.clear();
        const auto centre = area.getY() + (static_cast<float>(channel) + 0.5f) * channelHeight;
        const auto scale = channelHeight * 0.46f;
        for (int pixel = 0; pixel < width; ++pixel)
        {
            float low = 0, high = 0;
            const auto begin = from + (to - from) * pixel / width, end = from + (to - from) * (pixel + 1) / width;
            thumbnail.getApproximateMinMax(begin, end, channel, low, high);
            const auto x = area.getX() + static_cast<float>(pixel), y = centre - juce::jlimit(0.0f, 1.0f, high) * scale;
            if (pixel == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
            bottom.push_back(centre - juce::jlimit(-1.0f, 0.0f, low) * scale);
        }
        for (int pixel = width; pixel-- > 0;) path.lineTo(area.getX() + static_cast<float>(pixel), bottom[static_cast<size_t>(pixel)]);
        path.closeSubPath();
    }
    return path;
}
namespace
{
juce::File appearanceFile()
{ return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Opentone/appearance.json"); }
}
namespace
{
const std::array<juce::uint32, 6> lightColours { 0xffa5a5a5, 0xff818181, 0xffcdf8ff, 0xff121212, 0xff4f4f4f, 0xffffb901 };
const std::array<juce::uint32, 6> darkColours { 0xff363636, 0xff2a2a2a, 0xffb0ddeb, 0xffb5b5b5, 0xff111111, 0xffffad56 };
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
void resetUiColours() { setUiThemePreset(true); }
void loadUiColours()
{
    const auto value = juce::JSON::parse(appearanceFile()); if (!value.isObject()) return;
    for (auto entry : { std::pair<const char*, juce::Colour*> {"background", &background}, {"panel", &panel}, {"accent", &accent}, {"ink", &ink}, {"line", &line}, {"primary", &primary} })
        if (value[entry.first].isString() && value[entry.first].toString().containsOnly("0123456789abcdefABCDEF") && value[entry.first].toString().length() == 8)
            *entry.second = juce::Colour::fromString(value[entry.first].toString()).withAlpha(1.0f);
    if (static_cast<int>(value["version"]) < 5)
    { setUiThemePreset(background.getPerceivedBrightness() < 0.5f); saveUiColours(); }
}
void saveUiColours()
{
    auto* object = new juce::DynamicObject(); juce::var value(object); object->setProperty("version", 5);
    for (auto entry : { std::pair<const char*, juce::Colour*> {"background", &background}, {"panel", &panel}, {"accent", &accent}, {"ink", &ink}, {"line", &line}, {"primary", &primary} })
        object->setProperty(entry.first, entry.second->toString());
    auto file = appearanceFile(); file.getParentDirectory().createDirectory(); file.replaceWithText(juce::JSON::toString(value));
}
void configureUiTheme(juce::LookAndFeel_V4& theme)
{
        theme.setColourScheme({ panel, background, panel, line, ink, line, ink, accent, ink });
        theme.setColour(juce::TextButton::buttonColourId, uiTone(UiTone::control));
        theme.setColour(juce::TextButton::buttonOnColourId, primary);
        theme.setColour(juce::TextButton::textColourOffId, ink);
        theme.setColour(juce::TextButton::textColourOnId, primary.contrasting());
        theme.setColour(juce::Label::textColourId, ink);
        theme.setColour(juce::ListBox::backgroundColourId, background);
        theme.setColour(juce::ListBox::textColourId, ink);
        theme.setColour(juce::TreeView::backgroundColourId, background);
        theme.setColour(juce::TreeView::linesColourId, line);
        theme.setColour(juce::DirectoryContentsDisplayComponent::textColourId, ink);
        theme.setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, accent);
        theme.setColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId, uiTone(UiTone::selectionText));
        theme.setColour(juce::ListBox::outlineColourId, line);
        theme.setColour(juce::TextEditor::backgroundColourId, uiTone(UiTone::control));
        theme.setColour(juce::TextEditor::textColourId, ink);
        theme.setColour(juce::TextEditor::outlineColourId, line);
        theme.setColour(juce::TextEditor::focusedOutlineColourId, ink);
        theme.setColour(juce::CaretComponent::caretColourId, ink);
        theme.setColour(juce::Slider::textBoxTextColourId, background.getPerceivedBrightness() < 0.5f ? uiTone(UiTone::range) : ink);
        theme.setColour(juce::Slider::textBoxBackgroundColourId, uiTone(UiTone::control));
        theme.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        theme.setColour(juce::PopupMenu::backgroundColourId, background.brighter(0.08f));
        theme.setColour(juce::PopupMenu::textColourId, ink);
        theme.setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
        theme.setColour(juce::PopupMenu::highlightedTextColourId, uiTone(UiTone::selectionText));
        theme.setColour(juce::ScrollBar::thumbColourId, line);
}
std::unique_ptr<juce::LookAndFeel_V4> createUiTheme() { return std::make_unique<ArrangementLook>(); }
}
