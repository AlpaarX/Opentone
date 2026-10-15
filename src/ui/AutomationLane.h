// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ui/UiTheme.h"
#include "application/EditorPresenter.h"
#include "domain/TimelineMath.h"
namespace opentone
{
class AutomationLane final : public juce::Component
{
public:
    AutomationLane(EditorPresenter& owner, juce::String track) : presenter(owner), id(std::move(track))
    { setWantsKeyboardFocus(true); setTitle("Automation: click to add; drag empty space to select; drag points to edit; copy, cut, paste, duplicate or Delete"); }
    ~AutomationLane() override { finish(true); }
    void setView(double start, double scale) { if (mode < 0 && editingParameter != presenter.automationParameter(id)) { selected.clear(); editingParameter = presenter.automationParameter(id); } from = start; zoom = scale; repaint(); }
    void paint(juce::Graphics& g) override
    {
        const auto* track = findTrack(presenter.project(), id); if (!track) return;
        const auto parameter = presenter.automationParameter(id); const auto& points = presenter.automation(id);
        const auto visible = g.getClipBounds(); const auto left = from + visible.getX() / zoom, right = from + visible.getRight() / zoom;
        g.setColour(background.withAlpha(0.18f)); g.fillRect(visible);
        g.setColour(juce::Colour(0xffff694c));
        const auto fallback = automationFallback(*track, parameter);
        const auto y = yAt(automationValue(points, left, fallback));
        if (points.empty())
        { const float dots[] {2, 4}; g.drawDashedLine({static_cast<float>(visible.getX()), y, static_cast<float>(visible.getRight()), y}, dots, 2, 1.5f);
          if (mode == 2 && moved) { g.setColour(accent.contrasting()); g.drawRect(selectionBox(), 1); } return; }
        juce::Path path; path.startNewSubPath(static_cast<float>(visible.getX()), y);
        if (parameter == 2)
        { for (size_t i = 0; i < points.size(); ++i) if (points[i].time >= left && points[i].time <= right)
          { const auto before = i > 0 ? points[i - 1].value : points[i].value; path.lineTo(xAt(points[i].time), yAt(before)); path.lineTo(xAt(points[i].time), yAt(points[i].value)); } }
        else for (int x = visible.getX() + 4; x < visible.getRight(); x += 4) path.lineTo(static_cast<float>(x), yAt(automationValue(points, from + x / zoom, fallback)));
        path.lineTo(static_cast<float>(visible.getRight()), yAt(automationValue(points, right, fallback)));
        g.strokePath(path, juce::PathStrokeType(1.5f));
        for (size_t i = 0; i < points.size(); ++i) if (points[i].time >= left && points[i].time <= right)
        { g.setColour(selected.count(static_cast<int>(i)) != 0 ? juce::Colours::white : juce::Colour(0xffff694c));
          g.fillEllipse(xAt(points[i].time) - 3, yAt(points[i].value) - 3, 6, 6); }
        if (mode == 2 && moved) { g.setColour(accent.contrasting().withAlpha(0.12f)); g.fillRect(selectionBox()); g.setColour(accent.contrasting()); g.drawRect(selectionBox(), 1); }
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        presenter.selectTrack(id); grabKeyboardFocus(); const auto* track = findTrack(presenter.project(), id); if (!track) return;
        const auto& points = presenter.automation(id);
        for (auto i = selected.begin(); i != selected.end();) if (*i < 0 || *i >= static_cast<int>(points.size())) i = selected.erase(i); else ++i;
        active = -1; down = last = event.position; insertion = timeAt(event);
        for (size_t i = 0; i < points.size(); ++i) if (event.position.getDistanceFrom({xAt(points[i].time), yAt(points[i].value)}) <= 8) { active = static_cast<int>(i); break; }
        additive = event.mods.isShiftDown() || event.mods.isCommandDown() || event.mods.isCtrlDown();
        if (active >= 0) { if (additive) { if (selected.count(active)) selected.erase(active); else selected.insert(active); } else if (!selected.count(active)) { selected.clear(); selected.insert(active); } }
        else if (!additive && !event.mods.isPopupMenu()) selected.clear();
        if (event.mods.isPopupMenu())
        {
            juce::PopupMenu menu; menu.addItem(1, "Copy", !selected.empty()); menu.addItem(2, "Cut", !selected.empty()); menu.addItem(3, "Paste", !presenter.copiedAutomation().empty());
            menu.addItem(4, "Duplicate", !selected.empty()); menu.addItem(5, "Delete", !selected.empty()); menu.addItem(6, "Clear automation", !points.empty());
            juce::Component::SafePointer<AutomationLane> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int choice)
            { if (!safe) return; if (choice == 1) safe->copy(); if (choice == 2) { safe->copy(); safe->remove(); } if (choice == 3) safe->paste();
              if (choice == 4) safe->duplicate(); if (choice == 5) safe->remove(); if (choice == 6) { safe->presenter.setAutomation(safe->id, safe->presenter.automationParameter(safe->id), {}); safe->selected.clear(); } }); repaint(); return;
        }
        if (!event.mods.isLeftButtonDown()) return;
        original = points; before = selected; editingParameter = presenter.automationParameter(id); mode = active >= 0 ? 1 : 2; moved = false;
        if (mode == 1 && !selected.empty()) { dragging = true; presenter.beginGesture(); }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (mode < 0 || event.getDistanceFromDragStart() < 3) return; moved = true; last = event.position;
        if (mode == 2)
        {
            selected = additive ? before : std::set<int>(); const auto box = selectionBox();
            for (size_t i = 0; i < original.size(); ++i) if (box.contains(juce::Point<float> {xAt(original[i].time), yAt(original[i].value)})) selected.insert(static_cast<int>(i));
            repaint(); return;
        }
        if (!dragging || selected.empty()) return;
        auto points = original;
        double low = -std::numeric_limits<double>::max(), high = std::numeric_limits<double>::max(), valueLow = -2, valueHigh = 2;
        for (const auto i : selected)
        {
            const auto& point = original[static_cast<size_t>(i)]; low = std::max(low, -point.time);
            if (i > 0 && !selected.count(i - 1)) low = std::max(low, original[static_cast<size_t>(i - 1)].time - point.time + std::min(1e-6, (point.time - original[static_cast<size_t>(i - 1)].time) / 4));
            if (i + 1 < static_cast<int>(original.size()) && !selected.count(i + 1)) high = std::min(high, original[static_cast<size_t>(i + 1)].time - point.time - std::min(1e-6, (original[static_cast<size_t>(i + 1)].time - point.time) / 4));
            const auto limits = automationLimits(editingParameter); valueLow = std::max(valueLow, limits.first - point.value); valueHigh = std::min(valueHigh, limits.second - point.value);
        }
        const auto delta = juce::jlimit(low, high, timeAt(event) - original[static_cast<size_t>(active)].time);
        const auto valueDelta = juce::jlimit(valueLow, valueHigh, valueAt(event.position.y) - original[static_cast<size_t>(active)].value);
        for (const auto i : selected) { points[static_cast<size_t>(i)].time += delta; points[static_cast<size_t>(i)].value += valueDelta; }
        presenter.setAutomation(id, editingParameter, std::move(points)); repaint();
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (mode == 2 && !moved && !additive)
        {
            const auto* track = findTrack(presenter.project(), id);
            if (track) { auto points = presenter.automation(id);
                const auto next = std::lower_bound(points.begin(), points.end(), insertion, [](const auto& point, double t) { return point.time < t; });
                const auto index = static_cast<int>(next - points.begin());
                if (next != points.end() && std::abs(next->time - insertion) < 1e-9) next->value = valueAt(event.position.y);
                else if (points.size() < 4096) points.insert(next, {insertion, valueAt(event.position.y)}); else { presenter.message("Automation has reached the 4096-point limit."); mode = -1; return; }
                selected = {index}; presenter.setAutomation(id, presenter.automationParameter(id), std::move(points)); }
        }
        finish(false); mode = -1; repaint();
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto code = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
        if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        {
            if (code == 'a') { selected.clear(); if (const auto* track = findTrack(presenter.project(), id)) { const auto& points = presenter.automation(id); for (size_t i = 0; i < points.size(); ++i) selected.insert(static_cast<int>(i)); } repaint(); return true; }
            if (code == 'c') { copy(); return true; } if (code == 'x') { copy(); remove(); return true; }
            if (code == 'v') { paste(); return true; } if (code == 'd') { duplicate(); return true; }
        }
        if (key.getKeyCode() == juce::KeyPress::escapeKey) { finish(true); return true; }
        if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) { remove(); return true; }
        return false;
    }
private:
    juce::Rectangle<float> selectionBox() const { return {std::min(down.x, last.x), std::min(down.y, last.y), std::abs(down.x - last.x), std::abs(down.y - last.y)}; }
    void finish(bool cancel) { if (dragging) { dragging = false; presenter.endGesture(cancel); } mode = -1; }
    std::vector<AutomationPoint> capture() const
    { std::vector<AutomationPoint> copied; if (const auto* track = findTrack(presenter.project(), id)) { const auto& points = presenter.automation(id);
      for (const auto i : selected) if (i >= 0 && i < static_cast<int>(points.size())) copied.push_back(points[static_cast<size_t>(i)]); } return copied; }
    void copy() { const auto points = capture(); if (!points.empty()) presenter.copyAutomation(points, presenter.automationParameter(id)); }
    void remove()
    { finish(false); const auto* track = findTrack(presenter.project(), id); if (!track || selected.empty()) return;
      auto points = presenter.automation(id);
      for (auto i = selected.rbegin(); i != selected.rend(); ++i) if (*i >= 0 && *i < static_cast<int>(points.size())) points.erase(points.begin() + *i);
      presenter.setAutomation(id, presenter.automationParameter(id), std::move(points)); selected.clear(); repaint(); }
    void insert(std::vector<AutomationPoint> copied, double start, int sourceParameter)
    {
        finish(false); if (copied.empty()) return; const auto* track = findTrack(presenter.project(), id); if (!track) return;
        const auto parameter = presenter.automationParameter(id); auto points = presenter.automation(id);
        const auto offset = start - copied.front().time; selected.clear(); std::vector<double> times;
        for (auto point : copied)
        {
            point.time += offset; const auto sourceLimits = automationLimits(sourceParameter), targetLimits = automationLimits(parameter);
            point.value = juce::jlimit(targetLimits.first, targetLimits.second, targetLimits.first + (point.value - sourceLimits.first) / (sourceLimits.second - sourceLimits.first) * (targetLimits.second - targetLimits.first));
            const auto next = std::lower_bound(points.begin(), points.end(), point.time, [](const auto& p, double t) { return p.time < t; });
            if (next != points.end() && std::abs(next->time - point.time) < 1e-9) { point.time = next->time; *next = point; }
            else points.insert(next, point); times.push_back(point.time);
        }
        if (points.size() > 4096) { presenter.message("Automation has reached the 4096-point limit."); return; }
        for (size_t i = 0; i < points.size(); ++i) if (std::binary_search(times.begin(), times.end(), points[i].time)) selected.insert(static_cast<int>(i));
        presenter.setAutomation(id, parameter, std::move(points)); repaint();
    }
    void paste() { insert(presenter.copiedAutomation(), insertion, presenter.copiedAutomationParameter()); }
    void duplicate()
    { const auto points = capture(); if (points.empty()) return; insert(points, points.back().time + timelineGridStep(presenter.project().bpm, zoom), presenter.automationParameter(id)); }
    float xAt(double time) const { return static_cast<float>((time - from) * zoom); }
    float yAt(double value) const { const auto limits = automationLimits(presenter.automationParameter(id)); const auto normalized = (value - limits.first) / (limits.second - limits.first); return 4 + static_cast<float>(1 - normalized) * static_cast<float>(std::max(1, getHeight() - 8)); }
    double valueAt(float y) const { const auto normalized = juce::jlimit(0.0, 1.0, 1 - (y - 4) / std::max(1.0, static_cast<double>(getHeight() - 8))); const auto parameter = presenter.automationParameter(id); const auto limits = automationLimits(parameter); return parameter == 2 ? (normalized >= 0.5 ? 1 : 0) : limits.first + normalized * (limits.second - limits.first); }
    double timeAt(const juce::MouseEvent& event) const { return snapToGrid(from + event.position.x / zoom, timelineGridStep(presenter.project().bpm, zoom), presenter.viewState().snap && !event.mods.isAltDown()); }
    EditorPresenter& presenter; juce::String id; double from = 0, zoom = 64;
    std::set<int> selected, before; int active = -1, mode = -1; bool dragging = false, additive = false, moved = false; int editingParameter = 0;
    juce::Point<float> down, last; double insertion = 0; std::vector<AutomationPoint> original;
};
}
