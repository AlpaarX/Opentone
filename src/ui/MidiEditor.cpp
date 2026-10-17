// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/MidiEditor.h"
#include "ui/UiTheme.h"
#include "ui/WaveformDetail.h"
#include "domain/TimelineMath.h"
namespace opentone
{
namespace { constexpr int keyWidth = 52; }
class MidiEditor::Roll final : public juce::Component
{
public:
    explicit Roll(MidiEditor& editor) : owner(editor)
    { setTitle("MIDI piano roll; double click to add; click or drag box to select; drag note edges to stretch"); setWantsKeyboardFocus(true); addChildComponent(needle); needle.setInterceptsMouseClicks(false, false); }
    double zoom() const { return owner.presenter.viewState().pianoZoom; }
    int keyHeight() const { return owner.presenter.viewState().pianoKeyHeight; }
    const Clip* clip() const { return findId(owner.presenter.project().clips, owner.clipId); }
    void updatePlayback()
    {
        const auto* value = clip(); const auto position = value && value->scene < 0 ? owner.presenter.position() : -1;
        const bool visible = owner.showPiano && value && position >= value->start && position < value->start + value->duration;
        if (visible)
        {
            const auto x = keyWidth + midiSourceTime(*value, position - value->start) * zoom();
            const bool inView = x >= owner.viewport.getViewPositionX() + keyWidth && x < owner.viewport.getViewPositionX() + owner.viewport.getViewWidth();
            needle.setVisible(inView);
            if (inView) needle.setBounds(static_cast<int>(x), 0, 2, getHeight());
        }
        else needle.setVisible(false);
    }
    double step() const { return timelineGridStep(owner.presenter.project().bpm, zoom()); }
    double time(float x) const { return snapToGrid((x - keyWidth) / zoom(), step(), true); }
    juce::Rectangle<float> visibleBounds(const MidiNote& note, const Clip&) const
    {
        const auto from = std::max(0.0, note.start), to = std::min(note.start + note.duration, std::max(0.0, (getWidth() - keyWidth) / zoom()));
        if (to <= from) return {};
        return { static_cast<float>(keyWidth + from * zoom()),
            static_cast<float>((127 - note.pitch) * keyHeight()), static_cast<float>((to - from) * zoom()), keyHeight() - 2.0f };
    }
    int hit(juce::Point<float> point) const
    {
        if (point.x < static_cast<float>(owner.viewport.getViewPositionX() + keyWidth)) return -1;
        if (const auto* value = clip())
            for (size_t i = value->notes.size(); i-- > 0;)
                if (visibleBounds(value->notes[i], *value).contains(point)) return static_cast<int>(i);
        return -1;
    }
    int edge(int index, juce::Point<float> point) const
    {
        const auto* value = clip(); if (!value || index < 0) return -1;
        const auto r = visibleBounds(value->notes[static_cast<size_t>(index)], *value);
        const auto grip = std::min(6.0f, r.getWidth() / 3);
        return point.x < r.getX() + grip ? 1 : point.x >= r.getRight() - grip ? 2 : 0;
    }
    std::bitset<128> pressed() const
    {
        std::bitset<128> result;
        if (mousePitch >= 0) result.set(static_cast<size_t>(mousePitch));
        if (juce::Time::getMillisecondCounterHiRes() < auditionUntil) result |= auditionPitches;
        return result;
    }
    void audition(const MidiNote& note)
    { owner.presenter.previewNote(owner.trackId, note.pitch, note.velocity);
      auditionPitches.reset(); auditionPitches.set(static_cast<size_t>(note.pitch)); auditionUntil = juce::Time::getMillisecondCounterHiRes() + 250; owner.timerCallback(); }
    void auditionSelection()
    {
        const auto* value = clip(); if (!value || selected.empty()) return;
        std::vector<MidiNote> notes; auditionPitches.reset();
        for (const auto i : selected) if (i < value->notes.size())
        { notes.push_back(value->notes[i]); auditionPitches.set(static_cast<size_t>(value->notes[i].pitch)); }
        owner.presenter.previewNotes(owner.trackId, notes);
        auditionUntil = juce::Time::getMillisecondCounterHiRes() + 250; owner.timerCallback();
    }
    void selectAll()
    {
        selected.clear();
        if (const auto* value = clip())
            for (size_t i = 0; i < value->notes.size(); ++i)
                if (!visibleBounds(value->notes[i], *value).isEmpty()) selected.insert(i);
        auditionSelection(); repaint();
    }
    void pruneSelection()
    { const auto* value = clip(); for (auto it = selected.begin(); it != selected.end();)
        if (!value || *it >= value->notes.size() || visibleBounds(value->notes[*it], *value).isEmpty()) it = selected.erase(it); else ++it; }
    void clearSelection() { selected.clear(); repaint(); }
    void setInsertion(double at) { insertTime = at; }
    std::vector<MidiNote> capture() const
    {
        std::vector<MidiNote> notes; const auto* value = clip(); if (!value) return notes;
        double start = std::numeric_limits<double>::max();
        for (const auto i : selected) if (i < value->notes.size()) { notes.push_back(value->notes[i]); start = std::min(start, value->notes[i].start); }
        for (auto& note : notes) note.start -= start;
        return notes;
    }
    void copy() { auto notes = capture(); if (!notes.empty()) owner.presenter.copyMidiNotes(std::move(notes)); }
    void paste(const std::vector<MidiNote>& notes, double at)
    {
        const auto* value = clip(); if (!value || notes.empty()) return;
        if (value->notes.size() + notes.size() > maxMidiNotes) { owner.presenter.message("MIDI clips support up to 4096 notes."); return; }
        auto edited = *value; const auto first = edited.notes.size();
        for (auto note : notes) { note.start += std::max(0.0, at); edited.notes.push_back(note); }
        owner.presenter.changeClip(edited); selected.clear();
        for (size_t i = first; i < edited.notes.size(); ++i) selected.insert(i);
        insertTime = at; owner.refresh(); auditionSelection(); revealSelection();
    }
    void paste() { paste(owner.presenter.midiNotesClipboard(), insertTime); }
    void duplicate()
    {
        const auto* value = clip(); if (!value || selected.empty()) return;
        double end = 0; for (const auto i : selected) if (i < value->notes.size()) end = std::max(end, value->notes[i].start + value->notes[i].duration);
        paste(capture(), end);
    }
    void nudge(double timeDelta, int pitchDelta, bool stretch)
    {
        const auto* value = clip(); if (!value || selected.empty() || gesture) return;
        auto edited = *value; pruneSelection(); if (selected.empty()) return;
        double minimum = -std::numeric_limits<double>::max(); int lower = -127, upper = 127;
        for (const auto i : selected)
        { const auto& note = value->notes[i]; minimum = std::max(minimum, stretch ? std::min(step(), note.duration) - note.duration : -note.start);
          lower = std::max(lower, -note.pitch); upper = std::min(upper, 127 - note.pitch); }
        timeDelta = std::max(minimum, timeDelta); pitchDelta = juce::jlimit(lower, upper, pitchDelta);
        for (const auto i : selected)
        { auto& note = edited.notes[i]; if (stretch) note.duration += timeDelta; else note.start += timeDelta; note.pitch += pitchDelta; }
        if (edited.notes == value->notes) return;
        owner.presenter.changeClip(edited); owner.refresh(); auditionSelection(); revealSelection();
    }
    void revealSelection()
    {
        const auto* value = clip(); if (!value || selected.empty()) return;
        const auto r = visibleBounds(value->notes[*selected.begin()], *value).toNearestInt(); auto position = owner.viewport.getViewPosition();
        if (r.getX() < position.x + keyWidth) position.x = std::max(0, r.getX() - keyWidth);
        else if (r.getRight() > position.x + owner.viewport.getViewWidth()) position.x = r.getRight() - owner.viewport.getViewWidth();
        if (r.getY() < position.y) position.y = r.getY();
        else if (r.getBottom() > position.y + owner.viewport.getViewHeight()) position.y = r.getBottom() - owner.viewport.getViewHeight();
        owner.viewport.setViewPosition(position);
    }
    void paint(juce::Graphics& g) override
    {
        const auto visible = g.getClipBounds(); g.fillAll(background);
        const auto keysX = owner.viewport.getViewPositionX();
        for (int pitch = 0; pitch < 128; ++pitch)
        {
            const auto y = (127 - pitch) * keyHeight();
            const bool black = juce::MidiMessage::isMidiNoteBlack(pitch);
            g.setColour(black ? background.darker(0.12f) : background.brighter(0.05f)); g.fillRect(0, y, getWidth(), keyHeight());
            const auto& project = owner.presenter.project();
            const auto degree = (pitch - project.scaleRoot + 120) % 12; const auto mask = project.scaleMode == 0 ? 0xab5 : 0x5ad;
            if (project.scaleEnabled && (mask & (1 << degree)) == 0) { g.setColour(juce::Colours::black.withAlpha(0.15f)); g.fillRect(0, y, getWidth(), keyHeight()); }
            const bool pushed = owner.activeNotes.test(static_cast<size_t>(pitch));
            g.setColour(pushed ? trackColour(owner.presenter.project(), owner.trackId) : black ? uiTone(UiTone::pianoBlack) : uiTone(UiTone::pianoWhite)); g.fillRect(keysX, y, keyWidth, keyHeight() - 1);
            if (pitch % 12 == 0) { g.setColour(pushed ? trackColour(owner.presenter.project(), owner.trackId).contrasting() : uiTone(UiTone::pianoWhite).contrasting()); g.setFont(juce::FontOptions(static_cast<float>(std::min(10, keyHeight() - 1))));
                g.drawText(juce::MidiMessage::getMidiNoteName(pitch, true, true, 4), keysX + 3, y, keyWidth - 4, keyHeight(), juce::Justification::centredLeft); }
        }
        g.reduceClipRegion(keysX + keyWidth, 0, getWidth() - keysX - keyWidth, getHeight());
        if (const auto* value = clip())
        {
            const auto start = static_cast<int>(std::min(static_cast<double>(getWidth()), keyWidth + (value->loop && value->loopLength > 0 ? value->loopStart : value->offset) * zoom()));
            const auto end = static_cast<int>(std::min(static_cast<double>(getWidth()), keyWidth + (value->loop && value->loopLength > 0 ? value->loopStart + value->loopLength : value->offset + value->duration) * zoom()));
            g.setColour(juce::Colours::black.withAlpha(0.25f)); g.fillRect(keyWidth, 0, std::max(0, start - keyWidth), getHeight());
            g.fillRect(end, 0, std::max(0, getWidth() - end), getHeight());
        }
        const auto spacing = step() * zoom();
        g.setColour(line);
        for (int i = std::max(0, static_cast<int>((visible.getX() - keyWidth) / spacing)); keyWidth + i * spacing < visible.getRight(); ++i)
            g.drawVerticalLine(static_cast<int>(keyWidth + i * spacing), static_cast<float>(visible.getY()), static_cast<float>(visible.getBottom()));
        if (const auto* value = clip())
        {
            for (size_t i = 0; i < value->notes.size(); ++i)
            {
                const auto r = visibleBounds(value->notes[i], *value);
                const auto colour = clipColour(owner.presenter.project(), *value);
                g.setColour(value->notes[i].enabled && value->notes[i].probability > 0 ? colour : colour.withMultipliedSaturation(0.1f).darker(0.3f)); g.fillRect(r);
                g.setColour(ink); g.drawRect(r, selected.count(i) ? 2 : 1);
                if (selected.count(i)) { g.setColour(juce::Colours::white); g.drawRect(r.reduced(2), 1); }
            }
            if (mode == 3)
            { const auto box = selectionBox(); g.setColour(trackColour(owner.presenter.project(), owner.trackId).withAlpha(0.3f)); g.fillRect(box);
              g.setColour(ink); g.drawRect(box, 1); }
            if (selected.empty()) { g.setColour(ink.withAlpha(0.6f)); g.drawVerticalLine(static_cast<int>(std::min(static_cast<double>(getWidth()), keyWidth + insertTime * zoom())),
                static_cast<float>(visible.getY()), static_cast<float>(visible.getBottom())); }
        }
    }
    void mouseMagnify(const juce::MouseEvent& event, float scale) override
    { if (std::isfinite(scale) && scale > 0) owner.setZoom(zoom() * scale, keyHeight(),
        event.getPosition() - owner.viewport.getViewPosition()); }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const auto anchor = event.getPosition() - owner.viewport.getViewPosition();
        if (event.mods.isAltDown()) owner.setZoom(zoom(), juce::roundToInt(keyHeight() * std::exp(wheel.deltaY)), anchor);
        else if (event.mods.isCommandDown() || event.mods.isCtrlDown()) owner.setZoom(zoom() * std::exp(wheel.deltaY * 2), keyHeight(), anchor);
        else juce::Component::mouseWheelMove(event, wheel);
    }
    void mouseMove(const juce::MouseEvent& e) override
    { const auto side = edge(hit(e.position), e.position);
      setMouseCursor(side == 1 ? bracketCursor(false) : side == 2 ? bracketCursor(true) : juce::MouseCursor::NormalCursor); }
    void mouseExit(const juce::MouseEvent&) override { if (!gesture) setMouseCursor(juce::MouseCursor::NormalCursor); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.x < owner.viewport.getViewPositionX() + keyWidth && e.mods.isLeftButtonDown())
        { mousePitch = juce::jlimit(0, 127, 127 - e.y / keyHeight());
          owner.presenter.previewNote(owner.trackId, mousePitch, 0.8, true); owner.timerCallback(); return; }
        const auto* value = clip(); if (!value || owner.presenter.gesturing()) return;
        pruneSelection(); before = selected; active = hit(e.position); down = last = e.position;
        insertTime = active >= 0 ? value->notes[static_cast<size_t>(active)].start : time(e.position.x);
        additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown(); dragged = false;
        copying = e.mods.isAltDown();
        if (e.mods.isRightButtonDown())
        { if (active >= 0) { if (!selected.count(static_cast<size_t>(active))) selected = { static_cast<size_t>(active) }; removeSelected(); } return; }
        if (!e.mods.isLeftButtonDown()) return;
        if (owner.presenter.viewState().drawMode && !additive && active < 0) { placeNote(e); return; }
        if (active < 0)
        { if (!additive) selected.clear(); mode = 3; grabKeyboardFocus(); repaint(); return; }
        const auto index = static_cast<size_t>(active);
        if (additive && selected.count(index)) { selected.erase(index); active = -1; repaint(); return; }
        if (!additive && !selected.count(index)) selected.clear(); selected.insert(index);
        originals = value->notes; sourceSelection = selected;
        mode = edge(active, e.position); owner.presenter.beginGesture(); gesture = true;
        auditionSelection(); grabKeyboardFocus(); repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    { if (!e.mods.isAltDown() && !owner.presenter.viewState().drawMode) placeNote(e); }
    void placeNote(const juce::MouseEvent& e)
    {
        const auto* value = clip(); if (!value || e.x < owner.viewport.getViewPositionX() + keyWidth || !e.mods.isLeftButtonDown() || hit(e.position) >= 0) return;
        finish(false); value = clip(); if (!value) return;
        if (value->notes.size() >= maxMidiNotes) { owner.presenter.message("MIDI clips support up to 4096 notes."); return; }
        const auto start = time(e.position.x);
        auto edited = *value; edited.notes.push_back({ juce::jlimit(0, 127, 127 - e.y / keyHeight()),
            start, step(), 0.8 });
        owner.presenter.changeClip(edited); selected = { edited.notes.size() - 1 };
        owner.refresh();
        audition(edited.notes.back()); grabKeyboardFocus(); repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (mousePitch >= 0)
        { const auto pitch = juce::jlimit(0, 127, 127 - e.y / keyHeight());
          if (pitch != mousePitch) { owner.presenter.releasePreview(owner.trackId, mousePitch); mousePitch = pitch;
            owner.presenter.previewNote(owner.trackId, mousePitch, 0.8, true); owner.timerCallback(); } return; }
        const auto* value = clip(); if (!value) return;
        if (mode == 3)
        { last = e.position; selected = additive ? before : std::set<size_t>();
          for (size_t i = 0; i < value->notes.size(); ++i) if (selectionBox().intersects(visibleBounds(value->notes[i], *value))) selected.insert(i);
          repaint(); return; }
        if (!gesture || active < 0 || e.getDistanceFromDragStart() < 3) return;
        auto edited = *value; edited.notes = originals; const auto& anchor = originals[static_cast<size_t>(active)];
        const auto point = anchor.start + (mode == 2 ? anchor.duration : 0);
        auto delta = snapToGrid(point + (e.position.x - down.x) / zoom(), step(), true) - point;
        double minimum = -std::numeric_limits<double>::max(), maximum = std::numeric_limits<double>::max();
        int pitchMinimum = -127, pitchMaximum = 127;
        for (const auto i : sourceSelection)
        {
            const auto& note = originals[i]; const auto shortest = std::min(step(), note.duration);
            minimum = std::max(minimum, mode == 2 ? shortest - note.duration : -note.start);
            if (mode == 1) maximum = std::min(maximum, note.duration - shortest);
            pitchMinimum = std::max(pitchMinimum, -note.pitch); pitchMaximum = std::min(pitchMaximum, 127 - note.pitch);
        }
        delta = juce::jlimit(minimum, maximum, delta);
        const auto pitchDelta = mode == 0 ? juce::jlimit(pitchMinimum, pitchMaximum, -juce::roundToInt((e.position.y - down.y) / keyHeight())) : 0;
        if (!dragged && std::abs(delta) < 1.0e-9 && pitchDelta == 0) return;
        const bool duplicate = copying && mode == 0;
        if (duplicate && originals.size() + sourceSelection.size() > maxMidiNotes)
        { owner.presenter.message("MIDI clips support up to 4096 notes."); return; }
        std::set<size_t> movedSelection; size_t movedActive = static_cast<size_t>(active);
        for (const auto i : sourceSelection)
        { if (duplicate) { if (i == static_cast<size_t>(active)) movedActive = edited.notes.size();
              movedSelection.insert(edited.notes.size()); edited.notes.push_back(originals[i]); }
          auto& note = edited.notes[duplicate ? edited.notes.size() - 1 : i];
          if (mode == 2) note.duration += delta;
          else { note.start += delta; if (mode == 1) note.duration -= delta; else note.pitch += pitchDelta; } }
        if (edited.notes == value->notes) return;
        const auto previous = value->notes[dragged && duplicate ? movedActive : static_cast<size_t>(active)]; dragged = true;
        owner.presenter.changeClip(edited);
        if (duplicate) selected = std::move(movedSelection);
        owner.refresh();
        const auto& moved = edited.notes[movedActive];
        if (moved.pitch != previous.pitch || !std::equal_to<double>()(moved.start, previous.start)) auditionSelection();
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override
    { if (gesture && !dragged && !additive && active >= 0) selected = { static_cast<size_t>(active) };
      const bool marquee = mode == 3; finish(false); if (marquee) auditionSelection(); mouseMove(e); }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == '0' && !key.getModifiers().isAnyModifierKeyDown() && !selected.empty())
        { if (const auto* value = clip()) { auto edited = *value; const auto enabled = !edited.notes[*selected.begin()].enabled;
            for (const auto index : selected) edited.notes[index].enabled = enabled; owner.presenter.changeClip(edited); repaint(); } return true; }
        if (key.getKeyCode() == juce::KeyPress::escapeKey) { finish(true); return true; }
        if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) { removeSelected(); return true; }
        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'a' || key.getKeyCode() == 'A')) { selectAll(); return true; }
        if (key.getModifiers().isCommandDown())
        { const auto code = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
          if (code == 'c') { copy(); return true; } if (code == 'v') { paste(); return true; } if (code == 'd') { duplicate(); return true; } }
        const auto code = key.getKeyCode(); const auto shift = key.getModifiers().isShiftDown();
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        { nudge(0, (code == juce::KeyPress::upKey ? 1 : -1) * (shift ? 12 : 1), false); return true; }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        { const auto delta = (code == juce::KeyPress::rightKey ? 1 : -1) * step();
          if (selected.empty()) { insertTime = std::max(0.0, insertTime + delta); repaint(); } else nudge(delta, 0, shift); return true; }
        return false;
    }
    void finish(bool cancel)
    {
        if (gesture) { gesture = false; owner.presenter.endGesture(cancel); }
        if (cancel) selected = before;
        if (mousePitch >= 0) { owner.presenter.releasePreview(owner.trackId, mousePitch); mousePitch = -1; }
        active = -1; mode = -1; setMouseCursor(juce::MouseCursor::NormalCursor); owner.timerCallback(); repaint();
    }
    ~Roll() override { if (gesture || mousePitch >= 0) finish(true); }
private:
    juce::Rectangle<float> selectionBox() const
    { return { std::min(down.x, last.x), std::min(down.y, last.y), std::abs(down.x - last.x), std::abs(down.y - last.y) }; }
    void removeSelected()
    { if (selected.empty()) return; finish(false);
      if (const auto* value = clip()) { auto edited = *value;
        for (auto it = selected.rbegin(); it != selected.rend(); ++it) if (*it < edited.notes.size()) edited.notes.erase(edited.notes.begin() + static_cast<std::ptrdiff_t>(*it));
        owner.presenter.changeClip(edited); selected.clear(); repaint(); } }
    class Needle final : public juce::Component
    { public: void paint(juce::Graphics& graphics) override { graphics.fillAll(ink); } } needle;
    MidiEditor& owner; std::vector<MidiNote> originals; std::set<size_t> selected, before, sourceSelection;
    juce::Point<float> down, last; int active = -1, mode = -1, mousePitch = -1;
    std::bitset<128> auditionPitches;
    double auditionUntil = 0, insertTime = 0; bool gesture = false, dragged = false, additive = false, copying = false;
};
class MidiEditor::Waveform final : public juce::Component, private juce::ChangeListener
{
public:
    explicit Waveform(MidiEditor& editor) : owner(editor), thumbnail(64, formats, cache)
    { addChildComponent(needle); needle.setInterceptsMouseClicks(false, false); formats.registerBasicFormats(); thumbnail.addChangeListener(this); detail.addChangeListener(this); setWantsKeyboardFocus(true); setTitle("Sample waveform; drag start/end poles to trim; Left/Right adjust start, Shift+Left/Right adjust end; pinch or Ctrl/Command-scroll to zoom, drag waveform to pan, double click to reset zoom"); }
    ~Waveform() override { finish(true); sourceLoader.removeAllJobs(true, -1); detail.removeChangeListener(this); thumbnail.removeChangeListener(this); }
    void updateNeedle()
    {
        const auto position = owner.presenter.samplerPosition(owner.trackId); const auto area = plot();
        const auto* track = findTrack(owner.presenter.project(), owner.trackId);
        const bool visible = isShowing() && position >= viewStart && position < viewStart + span() && track && position < track->sampleOffset + track->sampleDuration && span() > 0;
        needle.setVisible(visible);
        if (visible) needle.setBounds(juce::roundToInt(area.getX() + (position - viewStart) / span() * area.getWidth()), area.getY(), 1, area.getHeight());
    }
    void refresh()
    {
        const auto* track = findTrack(owner.presenter.project(), owner.trackId);
        const auto* asset = track ? findId(owner.presenter.project().assets, track->sample) : nullptr;
        const auto file = asset && !asset->missing ? owner.presenter.mediaDirectory().getChildFile(asset->path).getFullPathName() : juce::String();
        if (file != path)
        {
            thumbnail.clear(); path = file; zoom = 1; viewStart = 0; dirty = true; sourceError.clear();
            if (path.isNotEmpty())
            {
                juce::Component::SafePointer<Waveform> safe(this);
                sourceLoader.addJob([safe, file]
                {
                    juce::AudioFormatManager readerFormats; readerFormats.registerBasicFormats();
                    auto reader = std::make_shared<std::unique_ptr<juce::AudioFormatReader>>(readerFormats.createReaderFor(juce::File(file)));
                    juce::MessageManager::callAsync([safe, file, reader]
                    {
                        if (!safe || safe->path != file) return;
                        if (*reader) safe->thumbnail.setReader(reader->release(), file.hashCode64());
                        else safe->sourceError = "Could not read sample waveform";
                        safe->dirty = true; safe->repaint();
                    });
                });
            }
        }
        repaint();
    }
    void resized() override { dirty = true; }
    juce::Rectangle<int> plot() const { return getLocalBounds().reduced(7).withTrimmedBottom(22); }
    double sourceLength() const
    {
        const auto* track = findTrack(owner.presenter.project(), owner.trackId);
        const auto* asset = track ? findId(owner.presenter.project().assets, track->sample) : nullptr;
        return asset && asset->sampleRate > 0 ? static_cast<double>(asset->length) / asset->sampleRate : 0;
    }
    double span() const { return sourceLength() / zoom; }
    void setZoom(double next, float x)
    {
        const auto area = plot(); const auto length = sourceLength(); if (length <= 0 || area.getWidth() < 1) return;
        const auto position = juce::jlimit(0.0, 1.0, static_cast<double>(x - area.getX()) / area.getWidth());
        const auto anchor = viewStart + position * span(); zoom = juce::jlimit(1.0, 1024.0, next);
        viewStart = juce::jlimit(0.0, std::max(0.0, length - span()), anchor - position * span()); dirty = true; repaint();
    }
    void mouseMagnify(const juce::MouseEvent& event, float scale) override
    { if (std::isfinite(scale) && scale > 0) setZoom(zoom * scale, event.position.x); }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    { if (event.mods.isCtrlDown() || event.mods.isCommandDown()) setZoom(zoom * std::exp(wheel.deltaY * 2), event.position.x);
      else juce::Component::mouseWheelMove(event, wheel); }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (sourceLength() <= 0) return;
        if (event.mods.isLeftButtonDown()) marker = markerAt(event.position);
        if (marker >= 0) { grabKeyboardFocus(); owner.presenter.beginGesture(); setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); return; }
        dragStart = viewStart; downX = event.position.x;
        panning = event.mods.isLeftButtonDown() || event.mods.isMiddleButtonDown();
        if (panning) setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    { if (marker >= 0 && plot().getWidth() > 0) { moveMarker(viewStart + (event.position.x - plot().getX()) / plot().getWidth() * span()); return; }
      if (!panning || plot().getWidth() < 1) return;
      viewStart = juce::jlimit(0.0, std::max(0.0, sourceLength() - span()), dragStart - (event.position.x - downX) / plot().getWidth() * span()); dirty = true; repaint(); }
    void mouseUp(const juce::MouseEvent&) override { finish(false); }
    void mouseMove(const juce::MouseEvent& event) override
    { setMouseCursor(markerAt(event.position) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
    void mouseDoubleClick(const juce::MouseEvent&) override { finish(false); zoom = 1; viewStart = 0; dirty = true; repaint(); }
    void finish(bool cancel)
    { const bool editing = marker >= 0; marker = -1; panning = false;
      if (editing) owner.presenter.endGesture(cancel); setMouseCursor(juce::MouseCursor::NormalCursor); }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::escapeKey) { finish(true); return true; }
        if (key.getKeyCode() != juce::KeyPress::leftKey && key.getKeyCode() != juce::KeyPress::rightKey) return false;
        const auto* track = findTrack(owner.presenter.project(), owner.trackId);
        const auto* asset = track ? findId(owner.presenter.project().assets, track->sample) : nullptr;
        if (!asset || asset->missing || asset->sampleRate <= 0 || marker >= 0) return false;
        marker = key.getModifiers().isShiftDown() ? 1 : 0; owner.presenter.beginGesture();
        moveMarker(track->sampleOffset + (marker == 1 ? track->sampleDuration : 0) + (key.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1) / asset->sampleRate);
        finish(false); return true;
    }
    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(uiTone(UiTone::display)); const auto area = plot();
        const auto* track = findTrack(owner.presenter.project(), owner.trackId);
        const auto* asset = track ? findId(owner.presenter.project().assets, track->sample) : nullptr;
        const auto length = sourceLength();
        if (!track || !asset || asset->missing || length <= 0 || area.isEmpty() || sourceError.isNotEmpty())
        {
            graphics.setColour(juce::Colours::lightgrey); graphics.setFont(juce::FontOptions(12));
            const auto message = asset && asset->missing ? "Missing sample\n" + assetName(*asset) : sourceError.isNotEmpty() ? sourceError + (asset ? "\n" + assetName(*asset) : "") : !asset ? juce::String("Drop audio file or clip") : juce::String();
            graphics.drawFittedText(message, getLocalBounds().reduced(8), juce::Justification::centred, 3);
            return;
        }
        const auto colour = primary;
        graphics.setColour(colour);
        if (!detail.draw(graphics, path, area.toFloat(), viewStart, viewStart + span(), asset->sampleRate, colour))
        { if (dirty) { envelope = waveformEnvelope(thumbnail, area.toFloat(), viewStart, viewStart + span()); dirty = false; }
          graphics.fillPath(envelope); }
        graphics.setColour(juce::Colours::white.withAlpha(0.15f)); graphics.drawHorizontalLine(area.getCentreY(), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
        const auto xAt = [&](double seconds) { return area.getX() + (seconds - viewStart) / span() * area.getWidth(); };
        const auto from = juce::jlimit(area.getX(), area.getRight(), juce::roundToInt(xAt(track->sampleOffset)));
        const auto to = juce::jlimit(area.getX(), area.getRight(), juce::roundToInt(xAt(track->sampleOffset + track->sampleDuration)));
        graphics.setColour(juce::Colours::black.withAlpha(0.45f)); graphics.fillRect(area.withRight(from)); graphics.fillRect(area.withLeft(to));
        graphics.setColour(juce::Colours::white.withAlpha(0.9f));
        for (const auto seconds : { track->sampleOffset, track->sampleOffset + track->sampleDuration })
        { const auto rawX = juce::roundToInt(xAt(seconds)); const auto x = std::min(rawX, area.getRight() - 1);
          if (rawX >= area.getX() && rawX <= area.getRight())
          { graphics.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
            juce::Path pole; pole.addTriangle(static_cast<float>(x), static_cast<float>(area.getY()), static_cast<float>(x + (seconds > track->sampleOffset ? -10 : 10)), static_cast<float>(area.getY() + 5), static_cast<float>(x), static_cast<float>(area.getY() + 10)); graphics.fillPath(pole); } }
        graphics.setFont(juce::FontOptions(10)); graphics.setColour(juce::Colours::lightgrey.withAlpha(0.7f));
        for (int tick = 0; tick <= 4; ++tick)
        { const int x = area.getX() + area.getWidth() * tick / 4;
          const auto seconds = viewStart + span() * tick / 4;
          graphics.drawText(juce::String(seconds, span() < 1 ? 3 : 2) + " s", x - (tick == 4 ? 48 : 0), area.getBottom() + 1, 48, 12, juce::Justification::centredLeft); }
        const auto overview = juce::Rectangle<float>(static_cast<float>(area.getX()), static_cast<float>(getHeight() - 10), static_cast<float>(area.getWidth()), 3);
        graphics.setColour(juce::Colours::white.withAlpha(0.12f)); graphics.fillRect(overview);
        graphics.setColour(colour); graphics.fillRect(overview.withX(overview.getX() + static_cast<float>(viewStart / length) * overview.getWidth()).withWidth(static_cast<float>(span() / length) * overview.getWidth()));
    }
private:
    int markerAt(juce::Point<float> point) const
    {
        const auto* track = findTrack(owner.presenter.project(), owner.trackId);
        const auto* asset = track ? findId(owner.presenter.project().assets, track->sample) : nullptr;
        const auto area = plot();
        if (!track || !asset || asset->missing || span() <= 0 || !area.expanded(5).toFloat().contains(point)) return -1;
        const auto start = area.getX() + (track->sampleOffset - viewStart) / span() * area.getWidth();
        const auto end = area.getX() + (track->sampleOffset + track->sampleDuration - viewStart) / span() * area.getWidth();
        const auto near = [&](double x) { return x >= area.getX() && x <= area.getRight() && std::abs(point.x - x) <= 8; };
        if (near(end) && (!near(start) || std::abs(point.x - end) < std::abs(point.x - start))) return 1;
        return near(start) ? 0 : -1;
    }
    void moveMarker(double seconds)
    {
        const auto* track = findTrack(owner.presenter.project(), owner.trackId);
        const auto* asset = track ? findId(owner.presenter.project().assets, track->sample) : nullptr;
        if (!asset || asset->missing || asset->sampleRate <= 0 || asset->length < 1) return;
        const auto frame = 1 / asset->sampleRate, length = sourceLength();
        const auto oldEnd = track->sampleOffset + track->sampleDuration;
        seconds = std::round(seconds * asset->sampleRate) / asset->sampleRate;
        const auto start = marker == 0 ? juce::jlimit(0.0, std::max(0.0, oldEnd - frame), seconds) : track->sampleOffset;
        const auto end = marker == 1 ? juce::jlimit(std::min(length, start + frame), length, seconds) : oldEnd;
        owner.presenter.loadSampler(owner.trackId, track->sample, start, end - start);
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { dirty = true; repaint(); }
    class Needle final : public juce::Component
    { void paint(juce::Graphics& graphics) override { graphics.fillAll(juce::Colours::white.withAlpha(0.85f)); } } needle;
    MidiEditor& owner; juce::String path, sourceError; juce::ThreadPool sourceLoader {1}; juce::AudioFormatManager formats;
    WaveformDetail detail; juce::AudioThumbnailCache cache {4}; juce::AudioThumbnail thumbnail; juce::Path envelope;
    double zoom = 1, viewStart = 0, dragStart = 0; float downX = 0; bool dirty = true, panning = false; int marker = -1;
};
class MidiEditor::DeviceSurface final : public juce::Component
{
public:
    void paint(juce::Graphics& graphics) override
    { graphics.setColour(uiTone(UiTone::detail)); graphics.fillRoundedRectangle(getLocalBounds().toFloat(), 4);
      graphics.setColour(line); graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4, 1); }
};
class MidiEditor::DeviceHeader final : public juce::Component
{
public:
    explicit DeviceHeader(MidiEditor& editor) : owner(editor)
    { setTitle("Sampler instrument; drag to another MIDI track or press Delete to remove"); setWantsKeyboardFocus(true);
      setMouseCursor(juce::MouseCursor::DraggingHandCursor);
      addAndMakeVisible(enabled); enabled.getProperties().set("device-power", true); enabled.setClickingTogglesState(true); enabled.setTitle("Enable or disable Sampler");
      enabled.onClick = [this] { owner.presenter.bypassSampler(owner.trackId, !enabled.getToggleState()); }; }
    void update()
    { if (const auto* track = findTrack(owner.presenter.project(), owner.trackId)) enabled.setToggleState(!track->samplerBypass, juce::dontSendNotification); }
    void resized() override { enabled.setBounds(getWidth() - 19, 2, 14, 14); }
    void paint(juce::Graphics& graphics) override
    { graphics.fillAll(juce::Colour(0xff617f85)); graphics.setColour(juce::Colours::black);
      graphics.setFont(juce::FontOptions(11)); graphics.drawText("Sampler", getLocalBounds().withTrimmedRight(26).reduced(8, 0), juce::Justification::centredLeft);
      graphics.drawRect(getLocalBounds(), hasKeyboardFocus(true) ? 2 : 1); }
    void mouseDown(const juce::MouseEvent& event) override
    {
        owner.presenter.selectTrack(owner.trackId); grabKeyboardFocus();
        if (event.mods.isPopupMenu())
        { juce::PopupMenu menu; menu.addItem(1, "Delete");
          juce::WeakReference<EditorPresenter> presenterRef(&owner.presenter); const auto track = owner.trackId;
          menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [presenterRef, track](int choice)
          { if (presenterRef && choice == 1) presenterRef->removeSampler(track); }); }
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (event.getDistanceFromDragStart() < 4) return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            if (!container->isDragAndDropActive()) container->startDragging("instrument:" + owner.trackId, this, juce::ScaledImage(), true);
    }
    bool keyPressed(const juce::KeyPress& key) override
    { if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
      { owner.presenter.removeSampler(owner.trackId); return true; } return false; }
private: MidiEditor& owner; juce::TextButton enabled;
};
class MidiEditor::PluginCard final : public juce::Component
{
public:
    PluginCard(MidiEditor& editor, juce::String identifier) : id(std::move(identifier)), owner(editor)
    {
        if (const auto* plugin = model())
        { juce::PluginDescription description; const auto xml = juce::XmlDocument::parse(plugin->description);
          builtin = xml && description.loadFromXml(*xml) && description.pluginFormatName == "Opentone"; }
        setWantsKeyboardFocus(true); addAndMakeVisible(open); addAndMakeVisible(enabled);
        open.onClick = [this] { owner.presenter.plugins().showEditor(id); };
        enabled.getProperties().set("device-power", true); enabled.setTitle("Enable or disable device"); enabled.setClickingTogglesState(true);
        enabled.onClick = [this] { owner.presenter.bypassPlugin(owner.trackId, id, !enabled.getToggleState()); };
    }
    ~PluginCard() override { clearEditor(); }
    const Plugin* model() const
    { const auto* track = findTrack(owner.presenter.project(), owner.trackId); return track ? findId(track->plugins, id) : nullptr; }
    int preferredWidth() const { return embedded ? editorWidth + 16 : 192; }
    void update()
    {
        open.getProperties().set("midi-target", "plugin-device/" + id);
        const auto slot = owner.presenter.plugins().editorSlot(id);
        if (editingSlot != slot) { clearEditor(); editingSlot = slot; }
        if (!embedded && editingSlot && builtin && editingSlot->instance && !editingSlot->instance->getActiveEditor())
        { embedded.reset(editingSlot->instance->hasEditor() ? editingSlot->instance->createEditorAndMakeActive() : new juce::GenericAudioProcessorEditor(*editingSlot->instance)); if (embedded) { editorWidth = embedded->getWidth(); addAndMakeVisible(*embedded); } }
        if (embedded)
        {
            std::function<void(juce::Component&)> mark = [&](juce::Component& control)
            { if (control.getProperties()["plugin-parameter"].isString()) control.getProperties().set("midi-target", "plugin/" + id + "/" + control.getProperties()["plugin-parameter"].toString());
              for (int i = 0; i < control.getNumChildComponents(); ++i) mark(*control.getChildComponent(i)); };
            mark(*embedded);
        }
        open.setVisible(!builtin && !embedded);
        const bool unavailable = editingSlot && !editingSlot->loading && (!editingSlot->instance || editingSlot->error.isNotEmpty());
        open.setEnabled(editingSlot && editingSlot->instance && !unavailable); open.setButtonText(unavailable ? "Unavailable" : "Open");
        if (unavailable) open.getProperties().remove("midi-target");
        if (const auto* plugin = model()) enabled.setToggleState(!plugin->bypass, juce::dontSendNotification); resized(); repaint();
    }
    void paint(juce::Graphics& graphics) override
    {
        const auto* plugin = model(); if (!plugin) return;
        const bool unavailable = editingSlot && !editingSlot->loading && (!editingSlot->instance || editingSlot->error.isNotEmpty());
        graphics.fillAll(panel); graphics.setColour(unavailable ? juce::Colour(0xffb48179) : juce::Colour(0xff617f85)); graphics.fillRect(0, 0, getWidth(), 20);
        graphics.setColour(juce::Colours::black); graphics.setFont(juce::FontOptions(11));
        const auto* source = findTrack(owner.presenter.project(), plugin->sidechain);
        const auto title = plugin->name + (source ? " — Sidechain: " + source->name : juce::String());
        graphics.drawText((unavailable ? "Unavailable: " : "") + title, 6, 0, getWidth() - 34, 20, juce::Justification::centredLeft, true);
        graphics.setColour(ink); const auto message = owner.presenter.plugins().status(id);
        if (!embedded) graphics.drawFittedText(unavailable ? plugin->name + "\n" + message : message.isEmpty() ? (plugin->instrument ? "Instrument" : "Audio effect") : message,
                               8, 88, getWidth() - 16, getHeight() - 96, juce::Justification::topLeft, 3);
        graphics.setColour(hasKeyboardFocus(true) ? ink : line); graphics.drawRect(getLocalBounds(), hasKeyboardFocus(true) ? 2 : 1);
    }
    void resized() override
    { enabled.setBounds(getWidth() - 19, 2, 14, 14); open.setBounds(8, 27, getWidth() - 16, 20); if (embedded) embedded->setBounds(8, 22, getWidth() - 16, std::max(1, getHeight() - 24)); }
    void mouseDown(const juce::MouseEvent& event) override
    {
        owner.presenter.selectTrack(owner.trackId); grabKeyboardFocus();
        if (event.mods.isPopupMenu())
        { juce::PopupMenu menu; if (!builtin) menu.addItem(1, "Open plug-in", editingSlot && editingSlot->instance != nullptr); menu.addItem(2, "Delete");
          juce::WeakReference<EditorPresenter> presenterRef(&owner.presenter); const auto track = owner.trackId, plugin = id;
          menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [presenterRef, track, plugin](int choice)
          { if (!presenterRef) return; if (choice == 1) presenterRef->plugins().showEditor(plugin); if (choice == 2) presenterRef->removePlugin(track, plugin); }); }
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { if (!builtin) owner.presenter.plugins().showEditor(id); }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown() || event.getMouseDownY() >= 20 || event.getDistanceFromDragStart() < 4) return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            if (!container->isDragAndDropActive()) container->startDragging("plugin:" + owner.trackId + ":" + id, this, juce::ScaledImage(), true);
    }
    bool keyPressed(const juce::KeyPress& key) override
    { if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
      { const auto track = owner.trackId, plugin = id; owner.presenter.removePlugin(track, plugin); return true; } return false; }
    juce::String id;
private:
    void clearEditor()
    { if (embedded) embedded->getAudioProcessor()->editorBeingDeleted(embedded.get()); embedded.reset(); }
    MidiEditor& owner; juce::TextButton open {"Open plug-in"}, enabled {"On"};
    std::shared_ptr<PluginHost::Slot> editingSlot; std::unique_ptr<juce::AudioProcessorEditor> embedded; int editorWidth = 192; bool builtin = false;
};
MidiEditor::MidiEditor(EditorPresenter& owner)
    : presenter(owner), roll(std::make_unique<Roll>(*this)), waveform(std::make_unique<Waveform>(*this)), deviceHeader(std::make_unique<DeviceHeader>(*this)), samplerSurface(std::make_unique<DeviceSurface>())
{
    setWantsKeyboardFocus(true); setSize(760, 500); setTitle("Device View and MIDI piano roll");
    addAndMakeVisible(viewport); addAndMakeVisible(deviceViewport);
    for (auto* control : std::initializer_list<juce::Component*> { &loopToggle, &loopStartLabel, &loopStart, &loopLengthLabel, &loopLength, &clipLengthLabel, &clipLength }) addAndMakeVisible(control);
    for (auto* control : std::initializer_list<juce::Component*> { &horizontalZoom, &verticalZoom, &horizontalLabel, &verticalLabel }) addAndMakeVisible(control);
    label(horizontalLabel, "H"); label(verticalLabel, "V");
    horizontalZoom.setRange(32, 40000); horizontalZoom.setSkewFactorFromMidPoint(180); verticalZoom.setRange(8, 48, 1);
    horizontalZoom.setTitle("Piano roll time zoom; pinch or Ctrl/Command-scroll"); verticalZoom.setTitle("Piano roll note height; Alt/Option-scroll");
    for (auto* slider : { &horizontalZoom, &verticalZoom }) { slider->setSliderStyle(juce::Slider::LinearHorizontal); slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); }
    horizontalZoom.setDoubleClickReturnValue(true, 180); verticalZoom.setDoubleClickReturnValue(true, 14);
    horizontalZoom.onValueChange = [this] { setZoom(horizontalZoom.getValue(), presenter.viewState().pianoKeyHeight, { viewport.getWidth() / 2, viewport.getHeight() / 2 }); };
    verticalZoom.onValueChange = [this] { setZoom(presenter.viewState().pianoZoom, static_cast<int>(verticalZoom.getValue()), { viewport.getWidth() / 2, viewport.getHeight() / 2 }); };
    label(loopStartLabel, "Start (beats)"); label(loopLengthLabel, "Loop length"); label(clipLengthLabel, "Clip length");
    for (auto* slider : { &loopStart, &loopLength, &clipLength })
    {
        slider->setSliderStyle(juce::Slider::IncDecButtons); slider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 62, 20);
        slider->setNumDecimalPlacesToDisplay(3);
        slider->onDragStart = [this] { presenter.beginGesture(); }; slider->onDragEnd = [this] { presenter.endGesture(false); };
    }
    setSliderDefault(loopStart, 0); setSliderDefault(loopLength, 4); setSliderDefault(clipLength, 4);
    setSliderDefault(horizontalZoom, 180); setSliderDefault(verticalZoom, 14);
    loopToggle.onClick = [this]
    { if (const auto* clip = findId(presenter.project().clips, clipId)) { auto edited = *clip; edited.loop = loopToggle.getToggleState(); presenter.changeClip(edited); } };
    loopStart.onValueChange = [this]
    { if (const auto* clip = findId(presenter.project().clips, clipId))
      { auto edited = *clip; edited.loopStart = loopStart.getValue() * 60 / presenter.project().bpm;
        edited.offset = std::max(0.0, edited.offset + edited.loopStart - clip->loopStart); presenter.changeClip(edited); } };
    loopLength.onValueChange = [this]
    { if (const auto* clip = findId(presenter.project().clips, clipId))
      { auto edited = *clip; edited.loopLength = loopLength.getValue() * 60 / presenter.project().bpm; presenter.changeClip(edited); } };
    clipLength.onValueChange = [this]
    { if (const auto* clip = findId(presenter.project().clips, clipId))
      { auto edited = *clip; edited.duration = clipLength.getValue() * 60 / presenter.project().bpm; presenter.changeClip(edited); } };
    loopStart.setTitle("Loop start in beats"); loopLength.setTitle("Source loop length in beats"); clipLength.setTitle("Arrangement clip length in beats");
    deviceViewport.setViewedComponent(&deviceContent, false); deviceViewport.setScrollBarsShown(false, true); deviceViewport.getHorizontalScrollBar().setAutoHide(true);
    deviceViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    deviceContent.addMouseListener(this, false); deviceContent.addAndMakeVisible(*samplerSurface); deviceContent.addAndMakeVisible(dropEffects);
    dropEffects.setJustificationType(juce::Justification::centred); dropEffects.setInterceptsMouseClicks(false, false);
    for (auto* child : std::initializer_list<juce::Component*> { &load, &clips, &source, &rootLabel, &root }) samplerSurface->addAndMakeVisible(child);
    for (auto* child : std::initializer_list<juce::Component*> { waveform.get(), &volumeLabel, &volume, &oneShot, &sampleLoop }) samplerSurface->addAndMakeVisible(child);
    samplerSurface->addAndMakeVisible(*deviceHeader);
    label(rootLabel, "Root"); label(volumeLabel, "Gain");
    root.setRange(0, 127, 1); root.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); root.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18); setSliderDefault(root, 60);
    root.setColour(juce::Slider::textBoxTextColourId, ink); root.setColour(juce::Slider::textBoxBackgroundColourId, background);
    root.setTitle("Sampler root MIDI note; 60 is C4");
    root.onDragStart = [this] { presenter.beginGesture(); }; root.onDragEnd = [this] { presenter.endGesture(false); };
    root.onValueChange = [this] { presenter.setRootNote(trackId, static_cast<int>(root.getValue())); };
    volume.setRange(0, 2, 0.001); volume.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 18);
    setSliderDefault(volume, 1);
    volume.textFromValueFunction = [](double value) { return volumeText(value); };
    volume.valueFromTextFunction = [](const juce::String& text) { return (text.containsIgnoreCase("inf") || text.containsChar(0x221e)) ? 0.0 : std::pow(10.0, text.getDoubleValue() / 20); }; volume.setTitle("Sampler volume; instrument output gain");
    volume.updateText();
    volume.onValueChange = [this] { presenter.setSamplerGain(trackId, volume.getValue()); };
    volume.setColour(juce::Slider::textBoxTextColourId, ink); volume.setColour(juce::Slider::textBoxBackgroundColourId, background);
    volume.onDragStart = [this] { presenter.beginGesture(); }; volume.onDragEnd = [this] { presenter.endGesture(false); };
    oneShot.setTitle("Play the selected sample region through after note release");
    sampleLoop.setTitle("Repeat the selected sample region while a note is held");
    oneShot.onClick = [this] { presenter.setSamplerMode(trackId, oneShot.getToggleState(), false); };
    sampleLoop.onClick = [this] { presenter.setSamplerMode(trackId, false, sampleLoop.getToggleState()); };
    load.onClick = [this] { chooseSource(); };
    clips.setTitle("Choose audio clip for sampler"); clips.onClick = [this] { if (onChooseClip) onChooseClip(clips); };
    viewport.setViewedComponent(roll.get(), false); viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    presenter.addChangeListener(this); refresh(); viewport.setViewPosition(0, (127 - 72) * presenter.viewState().pianoKeyHeight); startTimerHz(30);
}
MidiEditor::~MidiEditor() { deviceViewport.setViewedComponent(nullptr, false); deviceContent.removeMouseListener(this); stopTimer(); presenter.removeChangeListener(this); roll->finish(true); }
void MidiEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(panel);
    if (showPiano)
    {
        graphics.setColour(background); graphics.fillRoundedRectangle(4, 4, 148, static_cast<float>(getHeight() - 8), 3);
        graphics.setColour(trackColour(presenter.project(), trackId)); graphics.fillRect(4, 4, 148, 20);
        graphics.setColour(trackColour(presenter.project(), trackId).contrasting()); graphics.setFont(juce::FontOptions(11));
        graphics.drawText("MIDI Clip", 10, 4, 134, 20, juce::Justification::centredLeft);
        graphics.setColour(ink); graphics.drawText("Notes", 163, 2, 80, 22, juce::Justification::centredLeft);
    }
}
void MidiEditor::mouseDown(const juce::MouseEvent& event)
{
    if (showPiano || !event.mods.isPopupMenu()) return;
    const auto* track = findTrack(presenter.project(), trackId); if (!track) return;
    juce::PopupMenu menu;
    if (track->midi && !track->sampler && !std::any_of(track->plugins.begin(), track->plugins.end(), [](const auto& plugin) { return plugin.instrument; })) menu.addItem(1, "Add instrument...");
    menu.addItem(2, "Add effect..."); juce::Component::SafePointer<MidiEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [safe](int choice)
    { if (safe && choice != 0 && safe->onAddDevice) safe->onAddDevice(*safe, choice == 1); });
}
void MidiEditor::paintDeviceOverview(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto scale = area.getWidth() / static_cast<float>(std::max(1, deviceContent.getWidth()));
    const auto draw = [&](const juce::Component& device)
    {
        auto box = area.withX(area.getX() + static_cast<float>(device.getX()) * scale).withWidth(static_cast<float>(device.getWidth()) * scale).reduced(1, 3);
        if (box.isEmpty()) return;
        g.setColour(background); g.fillRect(box); g.setColour(juce::Colour(0xff617f85)); g.fillRect(box.withHeight(2));
        g.setColour(ink.withAlpha(0.4f)); g.drawRect(box);
    };
    if (samplerSurface->isVisible()) draw(*samplerSurface);
    for (const auto& card : pluginCards) draw(*card);
}
void MidiEditor::scrollDevices(double proportion)
{
    const auto maximum = std::max(0, deviceContent.getWidth() - deviceViewport.getViewWidth());
    deviceViewport.setViewPosition(juce::roundToInt(juce::jlimit(0.0, 1.0, proportion) * maximum), 0);
}
std::pair<double, double> MidiEditor::deviceViewRange() const
{
    const auto width = std::max(1, deviceContent.getWidth());
    return { static_cast<double>(deviceViewport.getViewPositionX()) / width, std::min(1.0, static_cast<double>(deviceViewport.getViewWidth()) / width) };
}
void MidiEditor::cancelGesture() { waveform->finish(true); roll->finish(true); }
void MidiEditor::selectAllNotes() { roll->selectAll(); }
void MidiEditor::copyNotes() { roll->copy(); }
void MidiEditor::pasteNotes() { roll->paste(); }
void MidiEditor::duplicateNotes() { roll->duplicate(); }
bool MidiEditor::noteFocused() const { return roll->hasKeyboardFocus(true); }
void MidiEditor::setKeyboardNotes(std::bitset<128> notes) { keyboardNotes = notes; timerCallback(); }
void MidiEditor::timerCallback()
{ roll->updatePlayback(); waveform->updateNeedle();
  const auto pressed = presenter.activeNotes(trackId) | keyboardNotes | roll->pressed();
  if (pressed != activeNotes) { activeNotes = pressed; roll->repaint(); } }
void MidiEditor::setContext(juce::String track, juce::String clip, bool piano)
{
    const bool changed = trackId != track || clipId != clip;
    if (trackId != track || clipId != clip || showPiano != piano) { waveform->finish(true); roll->finish(true); }
    if (trackId != track || clipId != clip) roll->clearSelection();
    trackId = std::move(track); clipId = std::move(clip); showPiano = piano;
    volume.getProperties().set("midi-target", "track/" + trackId + "/sampler-volume");
    root.getProperties().set("midi-target", "track/" + trackId + "/sampler-root");
    resized(); refresh();
    if (changed) if (const auto* value = findId(presenter.project().clips, clipId))
    { roll->setInsertion(midiSourceTime(*value)); viewport.setViewPosition(static_cast<int>(std::min(10000000.0, midiSourceTime(*value) * presenter.viewState().pianoZoom)), viewport.getViewPositionY()); }
}
bool MidiEditor::keyPressed(const juce::KeyPress& key)
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (dynamic_cast<juce::TextEditor*>(focused) || (focused && focused->findParentComponentOfClass<juce::TextEditor>())) return false;
    if (key.getKeyCode() == juce::KeyPress::spaceKey) { presenter.togglePlayback(); return true; }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z'))
    { if (key.getModifiers().isShiftDown()) presenter.redo(); else presenter.undo(); return true; }
    return false;
}
void MidiEditor::resized()
{
    auto bounds = getLocalBounds().reduced(4); auto inspector = bounds.removeFromLeft(148).reduced(6, 0); bounds.removeFromLeft(6);
    inspector.removeFromTop(27); loopToggle.setBounds(inspector.removeFromTop(19)); inspector.removeFromTop(8);
    for (auto pair : { std::pair<juce::Label*, juce::Slider*> { &loopStartLabel, &loopStart }, { &loopLengthLabel, &loopLength }, { &clipLengthLabel, &clipLength } })
    { pair.first->setBounds(inspector.removeFromTop(15)); pair.second->setBounds(inspector.removeFromTop(20)); inspector.removeFromTop(6); }
    auto bar = bounds.removeFromTop(22); auto zoomControls = bar.removeFromRight(180);
    horizontalLabel.setBounds(zoomControls.removeFromLeft(15)); horizontalZoom.setBounds(zoomControls.removeFromLeft(65)); zoomControls.removeFromLeft(10);
    verticalLabel.setBounds(zoomControls.removeFromLeft(15)); verticalZoom.setBounds(zoomControls);
    viewport.setBounds(bounds); deviceViewport.setBounds(getLocalBounds().reduced(4)); refresh();
}
void MidiEditor::refresh()
{
    const auto* track = findTrack(presenter.project(), trackId);
    const auto* asset = track ? findId(presenter.project().assets, track->sample) : nullptr;
    const bool installed = track && track->sampler; deviceHeader->update();
    for (auto* control : std::initializer_list<juce::Component*> { &load, &clips, &root, &rootLabel, waveform.get(), deviceHeader.get(), &source, &volume, &volumeLabel, &oneShot, &sampleLoop }) control->setVisible(installed && !showPiano);
    viewport.setVisible(showPiano); deviceViewport.setVisible(!showPiano);
    bool rebuild = pluginCards.size() != (track ? track->plugins.size() : 0);
    if (!rebuild && track) for (size_t i = 0; i < pluginCards.size(); ++i) if (pluginCards[i]->id != track->plugins[i].id) rebuild = true;
    if (rebuild)
    { pluginCards.clear(); if (track) for (const auto& plugin : track->plugins)
      { auto card = std::make_unique<PluginCard>(*this, plugin.id); deviceContent.addAndMakeVisible(*card); pluginCards.push_back(std::move(card)); } }
    samplerSurface->setVisible(installed);
    const int samplerWidth = installed ? juce::jlimit(568, 848, juce::roundToInt(deviceViewport.getMaximumVisibleWidth() * 0.48)) : 0;
    int chainWidth = samplerWidth; for (auto& card : pluginCards) { card->update(); chainWidth += card->preferredWidth() + 8; }
    const auto availableWidth = deviceViewport.getMaximumVisibleWidth();
    deviceContent.setSize(chainWidth > availableWidth ? chainWidth + 180 : availableWidth, std::max(140, std::min(instrumentHeight - 16, deviceViewport.getMaximumVisibleHeight())));
    int deviceX = samplerWidth;
    for (auto& card : pluginCards) { card->setBounds(deviceX, 0, card->preferredWidth(), deviceContent.getHeight()); deviceX += card->preferredWidth() + 8; }
    dropEffects.setBounds(chainWidth, 0, std::max(0, deviceContent.getWidth() - chainWidth), deviceContent.getHeight());
    label(dropEffects, track && track->midi && !installed && !std::any_of(track->plugins.begin(), track->plugins.end(), [](const auto& plugin) { return plugin.instrument; })
        ? "Drop Instrument or Audio Effects Here" : "Drop Audio Effects Here");
    samplerSurface->setBounds(0, 0, samplerWidth > 0 ? samplerWidth - 8 : 0, deviceContent.getHeight());
    auto instrument = samplerSurface->getLocalBounds().reduced(6);
    deviceHeader->setBounds(instrument.removeFromTop(18)); instrument.removeFromTop(3);
    auto bar = instrument.removeFromTop(18);
    load.setBounds(bar.removeFromRight(48)); bar.removeFromRight(4); clips.setBounds(bar.removeFromRight(64)); source.setBounds(bar);
    auto controls = instrument.removeFromBottom(42); instrument.removeFromBottom(4);
    auto modes = instrument.removeFromLeft(84); instrument.removeFromLeft(4);
    oneShot.setBounds(modes.removeFromTop(24)); sampleLoop.setBounds(modes.removeFromTop(24));
    waveform->setBounds(instrument);
    const int column = controls.getWidth() / 2;
    for (auto pair : { std::pair<juce::Label*, juce::Slider*> { &rootLabel, &root }, { &volumeLabel, &volume } })
    { auto cell = controls.removeFromLeft(column).reduced(5, 0); pair.first->setBounds(cell.removeFromTop(13)); pair.second->setBounds(cell); }
    label(source, asset ? (asset->missing ? "Missing: " : "") + assetName(*asset) : "");
    source.setTooltip(asset ? asset->path : juce::String()); if (asset && asset->missing) source.setColour(juce::Label::textColourId, uiTone(UiTone::record));
    if (track) { root.setValue(track->rootNote, juce::dontSendNotification); volume.setValue(track->samplerGain, juce::dontSendNotification); }
    oneShot.setToggleState(track && track->samplerOneShot, juce::dontSendNotification);
    sampleLoop.setToggleState(track && track->samplerLoop, juce::dontSendNotification);
    const auto* value = findId(presenter.project().clips, clipId);
    for (auto* control : std::initializer_list<juce::Component*> { &loopToggle, &loopStartLabel, &loopStart, &loopLengthLabel, &loopLength, &clipLengthLabel, &clipLength, &horizontalZoom, &verticalZoom, &horizontalLabel, &verticalLabel }) control->setVisible(showPiano && value != nullptr);
    horizontalZoom.setValue(presenter.viewState().pianoZoom, juce::dontSendNotification); verticalZoom.setValue(presenter.viewState().pianoKeyHeight, juce::dontSendNotification);
    if (value)
    {
        const auto beat = 60 / presenter.project().bpm;
        loopToggle.setToggleState(value->loop, juce::dontSendNotification);
        loopStart.setRange(0, 1000000, 1.0 / 64);
        loopLength.setRange(std::max(1.0 / 64, 0.001 / beat), 1000000, 1.0 / 64);
        clipLength.setRange(1.0 / 64, 1000000, 1.0 / 64);
        loopStart.setValue(value->loopStart / beat, juce::dontSendNotification);
        loopLength.setValue(value->loopLength / beat, juce::dontSendNotification);
        clipLength.setValue(value->duration / beat, juce::dontSendNotification);
    }
    double horizon = value ? (value->loop && value->loopLength > 0 ? value->loopStart + value->loopLength : value->offset + value->duration) : 0;
    if (value) for (const auto& note : value->notes) horizon = std::max(horizon, note.start + note.duration);
    horizon += 240 / presenter.project().bpm;
    roll->setSize(std::max(viewport.getWidth() - viewport.getScrollBarThickness(),
        value ? keyWidth + static_cast<int>(std::min(10000000.0, horizon * presenter.viewState().pianoZoom)) : 0), 128 * presenter.viewState().pianoKeyHeight);
    roll->pruneSelection(); roll->repaint(); waveform->refresh(); timerCallback();
}
void MidiEditor::setZoom(double zoom, int height, juce::Point<int> anchor)
{
    if (presenter.gesturing()) return;
    const auto oldZoom = presenter.viewState().pianoZoom; const auto oldHeight = presenter.viewState().pianoKeyHeight;
    const auto position = viewport.getViewPosition();
    const auto time = (position.x + anchor.x - keyWidth) / oldZoom;
    const auto row = static_cast<double>(position.y + anchor.y) / oldHeight;
    presenter.setPianoZoom(zoom, height); refresh();
    viewport.setViewPosition(juce::roundToInt(juce::jlimit(0.0, 10000000.0, keyWidth + time * presenter.viewState().pianoZoom - anchor.x)),
                            std::max(0, juce::roundToInt(row * presenter.viewState().pianoKeyHeight - anchor.y)));
}
void MidiEditor::chooseSource()
{
    if (choosing) return; choosing = true; const auto destination = trackId;
    chooser = std::make_unique<juce::FileChooser>("Load sampler audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    juce::Component::SafePointer<MidiEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, destination](const juce::FileChooser& selected)
        { if (!safe) return; safe->choosing = false;
          if (selected.getResult().existsAsFile()) safe->presenter.importFiles({ selected.getResult().getFullPathName() }, destination, 0); });
}
void MidiEditor::filesDropped(const juce::StringArray& files, int, int)
{ if (!files.isEmpty()) presenter.importFiles({ files[0] }, trackId, 0); }
bool MidiEditor::isInterestedInDragSource(const SourceDetails& details)
{ return presenter.isBrowserDrag(details.description.toString()) || details.description.toString().startsWith("asset:") || details.description.toString().startsWith("clip:")
      || details.description.toString().startsWith("instrument:") || details.description.toString().startsWith("plugin:"); }
void MidiEditor::itemDropped(const SourceDetails& details)
{
    const auto text = details.description.toString();
    if (presenter.isBrowserDrag(text)) { presenter.dropBrowserItem(text, trackId); return; }
    if (text.startsWith("plugin:")) { presenter.dropPlugin(text, trackId); return; }
    if (text.startsWith("instrument:")) { presenter.moveSampler(text.substring(11), trackId); return; }
    if (text.startsWith("clip:"))
    { if (const auto* value = findId(presenter.project().clips, text.substring(5))) presenter.loadSampler(trackId, value->asset, value->offset, value->duration); }
    else if (const auto* asset = findId(presenter.project().assets, text.substring(6)))
        presenter.loadSampler(trackId, asset->id, 0, static_cast<double>(asset->length) / asset->sampleRate);
}
}
