// SPDX-License-Identifier: AGPL-3.0-only
#include "SelectionChecks.h"
namespace opentone
{
namespace
{
juce::Component* findTitled(juce::Component& component, const juce::String& title)
{
    if (component.getTitle().startsWith(title)) return &component;
    for (auto* child : component.getChildren()) if (auto* found = findTitled(*child, title)) return found;
    return nullptr;
}
juce::Component* findNamed(juce::Component& component, const juce::String& name)
{
    if (component.getName() == name) return &component;
    for (auto* child : component.getChildren()) if (auto* found = findNamed(*child, name)) return found;
    return nullptr;
}
}
bool selectionChecks(juce::Component& editor, EditorPresenter& presenter, ProjectSession& session)
{
    const auto expect = [](bool success, const char* message)
    { if (!success) juce::Logger::writeToLog(juce::String("Selection check failed: ") + message); return success; };
    auto project = session.project();
    auto first = project.clips[0], second = first;
    const auto track = first.track;
    project.tracks = { { track, "First" }, { newId(), "Second" }, { newId(), "Third" } };
    first.start = 0; second.id = newId(); second.track = project.tracks[1].id; second.start = 0.5;
    project.clips = { first, second };
    session.replace(project, session.mediaDirectory(), true);
    presenter.setZoom(64); presenter.setSnap(false); presenter.setScroll(0, 0); presenter.setTrackHeight(track, 72); presenter.selectClips({});
    session.dispatchPendingMessages(); presenter.dispatchPendingMessages();
    auto* canvas = findTitled(editor, "Arrangement timeline;");
    if (!expect(canvas != nullptr, "canvas is present")) return false;
    const auto event = [canvas](juce::Point<float> point, juce::Point<float> down, bool shift = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | (shift ? juce::ModifierKeys::shiftModifier : 0)),
            1, 0, 0, 0, 0, canvas, canvas, now, down, now, 1, point != down);
    };
    const auto click = [&](juce::Point<float> point, bool shift = false)
    { canvas->mouseDown(event(point, point, shift)); canvas->mouseUp(event(point, point, shift)); };
    const auto revision = session.snapshot().revision;
    canvas->mouseMove(event({ 4, 44 }, { 4, 44 })); const auto leftCursor = canvas->getMouseCursor();
    canvas->mouseMove(event({ 124, 44 }, { 124, 44 })); const auto rightCursor = canvas->getMouseCursor();
    if (!expect(leftCursor != juce::MouseCursor::NormalCursor && rightCursor != juce::MouseCursor::NormalCursor
                && leftCursor != rightCursor, "header ends use distinct bracket cursors")) return false;
    canvas->mouseMove(event({ 124, 80 }, { 124, 80 }));
    if (!expect(canvas->getMouseCursor() == juce::MouseCursor::NormalCursor, "waveform edges keep the normal cursor")) return false;
    canvas->mouseMove(event({ 32, 44 }, { 32, 44 }));
    if (!expect(canvas->getMouseCursor() == juce::MouseCursor::NormalCursor, "header centres keep the normal cursor")) return false;
    canvas->mouseMove(event({ 4, 44 }, { 4, 44 })); canvas->mouseExit(event({ -1, 44 }, { -1, 44 }));
    if (!expect(canvas->getMouseCursor() == juce::MouseCursor::NormalCursor, "leaving the header clears the bracket cursor")) return false;
    click({ 32, 44 }); click({ 64, 116 }, true);
    if (!expect(presenter.viewState().selectedClips.size() == 2, "Shift-click selects two clips")) return false;
    click({ 32, 44 }, true);
    if (!expect(presenter.viewState().selectedClips == juce::StringArray { second.id }, "Shift-click toggles a clip off")) return false;
    click({ 47, 80 });
    if (!expect(presenter.viewState().selectedClips.isEmpty() && presenter.viewState().timeSelection
                && std::abs(presenter.viewState().timeSelection->start - 0.5) < 1.0e-9
                && std::abs(presenter.viewState().timeSelection->end - 0.5) < 1.0e-9,
                "waveform click marks the nearest grid line without selecting the clip")) return false;
    const auto markerImage = canvas->createComponentSnapshot(canvas->getLocalBounds());
    if (!expect(markerImage.getPixelAt(32, 80) == juce::Colour(0xff315f88)
                && markerImage.getPixelAt(32, 150) != juce::Colour(0xff315f88)
                && markerImage.getPixelAt(32, 10) != juce::Colour(0xff315f88),
                "insertion line stays within one track")) return false;
    presenter.selectTime({ 0.5, 0.5, { project.tracks[0].id, project.tracks[1].id } });
    const auto multiTrackMarker = canvas->createComponentSnapshot(canvas->getLocalBounds());
    if (!expect(multiTrackMarker.getPixelAt(32, 80) == juce::Colour(0xff315f88)
                && multiTrackMarker.getPixelAt(32, 150) != juce::Colour(0xff315f88),
                "a point selection spanning tracks still has a one-track insertion line")) return false;
    canvas->mouseDown(event({ 124, 80 }, { 124, 80 }));
    canvas->mouseDrag(event({ 92, 80 }, { 124, 80 }));
    canvas->mouseUp(event({ 92, 80 }, { 124, 80 }));
    if (!expect(session.project() == project && session.snapshot().revision == revision,
                "dragging a waveform edge selects time without resizing")) return false;
    presenter.selectTime({ 0, 2, { first.track } });
    canvas->mouseDown(event({ 124, 44 }, { 124, 44 }));
    canvas->mouseDrag(event({ 92, 44 }, { 124, 44 }));
    canvas->mouseUp(event({ 92, 44 }, { 124, 44 }));
    if (!expect(std::abs(findId(session.project().clips, first.id)->duration - 1.5) < 1.0e-9,
                "right header end resizes the clip")) return false;
    session.undoEdit();
    canvas->mouseDown(event({ 4, 44 }, { 4, 44 }));
    canvas->mouseDrag(event({ 36, 44 }, { 4, 44 }));
    canvas->mouseUp(event({ 36, 44 }, { 4, 44 }));
    if (!expect(std::abs(findId(session.project().clips, first.id)->start - 0.5) < 1.0e-9
                && std::abs(findId(session.project().clips, first.id)->offset - 0.5) < 1.0e-9,
                "left header end trims timing and source offset")) return false;
    session.undoEdit();
    const auto rangeRevision = session.snapshot().revision;
    canvas->mouseDown(event({ 64, 160 }, { 64, 160 }));
    canvas->mouseDrag(event({ 32, 80 }, { 64, 160 }));
    canvas->mouseUp(event({ 32, 80 }, { 64, 160 }));
    const auto timeRange = presenter.viewState().timeSelection;
    if (!expect(timeRange && std::abs(timeRange->start - 0.5) < 1.0e-9 && std::abs(timeRange->end - 1) < 1.0e-9
                && timeRange->tracks.size() == 2 && session.snapshot().revision == rangeRevision,
                "reverse waveform drag selects time across tracks without edits")) return false;
    click({ 48, 44 });
    if (!expect(session.project() == project && session.snapshot().revision == rangeRevision
                && presenter.viewState().timeSelection == timeRange, "header click inside a range does not split audio")) return false;
    canvas->mouseDown(event({ 190, 80 }, { 190, 80 }));
    canvas->mouseDrag(event({ 200, 175 }, { 190, 80 }));
    editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if (!expect(presenter.viewState().timeSelection == timeRange && !session.gesturing(), "Escape restores time selection")) return false;
    canvas->mouseDown(event({ 48, 44 }, { 48, 44 }));
    canvas->mouseDrag(event({ 112, 116 }, { 48, 44 }));
    canvas->mouseUp(event({ 112, 116 }, { 48, 44 }));
    const auto* firstPart = findId(session.project().clips, first.id);
    const auto* secondPart = findId(session.project().clips, second.id);
    if (!expect(firstPart && secondPart && session.project().clips.size() == 6
                && std::abs(firstPart->start - 1.5) < 1.0e-9 && std::abs(firstPart->offset - 0.5) < 1.0e-9
                && std::abs(firstPart->duration - 0.5) < 1.0e-9 && firstPart->track == project.tracks[1].id
                && secondPart->track == project.tracks[2].id,
                "dragging a selected header splices and moves only the selected audio")) return false;
    session.undoEdit();
    if (!expect(session.project() == project, "one undo restores splice and movement")) return false;
    presenter.selectTime(*timeRange);
    canvas->mouseDown(event({ 48, 44 }, { 48, 44 }));
    canvas->mouseDrag(event({ 112, 116 }, { 48, 44 }));
    editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if (!expect(session.project() == project && presenter.viewState().timeSelection == timeRange,
                "Escape restores the unsplit clips and selected time")) return false;
    presenter.selectClips({ first.id, second.id });
    canvas->mouseDown(event({ 32, 44 }, { 32, 44 }));
    canvas->mouseDrag(event({ 96, 116 }, { 32, 44 }));
    canvas->mouseUp(event({ 96, 116 }, { 32, 44 }));
    const auto moved = session.project();
    if (!expect(std::abs(moved.clips[0].start - 1) < 1.0e-9 && std::abs(moved.clips[1].start - 1.5) < 1.0e-9
                && moved.clips[0].track == project.tracks[1].id && moved.clips[1].track == project.tracks[2].id,
                "group drag preserves time and track spacing")) return false;
    session.undoEdit();
    if (!expect(session.project() == project, "one undo restores the group move")) return false;
    session.redoEdit();
    canvas->mouseDown(event({ 96, 116 }, { 96, 116 }));
    canvas->mouseDrag(event({ -100, 116 }, { 96, 116 }));
    if (!expect(std::abs(session.project().clips[0].start) < 1.0e-9 && std::abs(session.project().clips[1].start - 0.5) < 1.0e-9,
                "left boundary clamps the entire group")) return false;
    editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if (!expect(session.project() == moved && !session.gesturing(), "Escape cancels the entire group drag")) return false;
    editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    if (!expect(session.project().clips.empty() && session.project().assets == project.assets,
                "Delete removes selected clips and keeps audio")) return false;
    session.undoEdit();
    if (!expect(session.project() == moved, "one undo restores group deletion")) return false;
    editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if (!expect(presenter.viewState().selectedClips.isEmpty(), "Escape clears idle selection")) return false;
    auto* commands = dynamic_cast<juce::ApplicationCommandTarget*>(&editor);
    if (!expect(commands != nullptr, "editor exposes its commands")) return false;
    juce::Array<juce::CommandID> ids; commands->getAllCommands(ids);
    const auto command = [&](const juce::String& name)
    {
        for (const auto id : ids)
        {
            juce::ApplicationCommandInfo info(id); commands->getCommandInfo(id, info);
            if (info.shortName == name) return commands->perform(juce::ApplicationCommandTarget::InvocationInfo(id));
        }
        return false;
    };
    if (!expect(command("Select all clips") && presenter.viewState().selectedClips.size() == 2, "Select All command selects every clip")) return false;
    presenter.selectTime({ 1.5, 2, { project.tracks[1].id, project.tracks[2].id } });
    if (!expect(command("Copy") && command("Duplicate") && session.project().clips.size() == 6,
                "Copy and Duplicate commands handle a partial time selection")) return false;
    session.undoEdit();
    if (!expect(session.project() == moved, "Duplicate command is one undo step")) return false;
    presenter.selectTime({ 3, 3, { project.tracks[0].id } });
    if (!expect(command("Paste") && session.project().clips.size() == 4
                && std::abs(session.project().clips[2].start - 3) < 1.0e-9,
                "Paste command uses the insertion marker")) return false;
    session.undoEdit();
    if (!expect(session.project() == moved, "Paste command is one undo step")) return false;
    auto* text = dynamic_cast<juce::TextEditor*>(findTitled(editor, "Tempo in beats per minute"));
    if (!expect(text != nullptr, "tempo text field is present")) return false;
    text->grabKeyboardFocus();
    if (!expect(!command("Copy") && !command("Paste") && !command("Duplicate"), "text editing keeps its clipboard shortcuts")) return false;
    canvas->grabKeyboardFocus();
    const auto zoomRevision = session.snapshot().revision;
    canvas->mouseMagnify(event({ 100, 80 }, { 100, 80 }), 2);
    if (!expect(std::abs(presenter.viewState().pixelsPerSecond - 128) < 1.0e-9 && presenter.viewState().scrollX == 100
                && session.snapshot().revision == zoomRevision, "pinch zoom keeps the pointer's time fixed without an edit")) return false;
    click({ 144, 80 });
    if (!expect(presenter.viewState().timeSelection && std::abs(presenter.viewState().timeSelection->start - 1.25) < 1.0e-9,
                "zoomed clicks snap to the finer visible grid")) return false;
    canvas->mouseMagnify(event({ 100, 80 }, { 100, 80 }), std::nanf(""));
    if (!expect(std::abs(presenter.viewState().pixelsPerSecond - 128) < 1.0e-9, "invalid pinch scale is ignored")) return false;
    presenter.selectTime({ 1.5, 2, { project.tracks[1].id, project.tracks[2].id } });
    presenter.dispatchPendingMessages();
    auto* row = findTitled(editor, "Track header;");
    if (!expect(row != nullptr, "track header is present")) return false;
    const auto rowEvent = [row](juce::Point<float> point, juce::Point<float> down)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, row, row, now, down, now, 1, point != down);
    };
    const auto resizeRevision = session.snapshot().revision;
    row->mouseMove(rowEvent({ 100, 70 }, { 100, 70 }));
    if (!expect(row->getMouseCursor() == juce::MouseCursor::UpDownResizeCursor, "track border advertises vertical resizing")) return false;
    row->mouseDown(rowEvent({ 100, 70 }, { 100, 70 })); row->mouseDrag(rowEvent({ 100, 118 }, { 100, 70 }));
    presenter.dispatchPendingMessages(); row->mouseUp(rowEvent({ 100, 118 }, { 100, 70 }));
    if (!expect(row->getHeight() == 120 && presenter.viewState().trackHeights.at(project.tracks[0].id) == 120
                && session.snapshot().revision == resizeRevision, "track resize changes layout without editing audio")) return false;
    click({ 176, 164 });
    if (!expect(presenter.viewState().selectedClips == juce::StringArray { first.id }, "clip header hit testing follows resized tracks")) return false;
    presenter.selectTime({ 0.5, 0.5, { project.tracks[0].id } });
    const auto resizedMarker = canvas->createComponentSnapshot(canvas->getLocalBounds());
    if (!expect(resizedMarker.getPixelAt(64, 140) == juce::Colour(0xff315f88)
                && resizedMarker.getPixelAt(64, 164) != juce::Colour(0xff315f88), "insertion line follows the resized track height")) return false;
    row->mouseDown(rowEvent({ 100, 118 }, { 100, 118 })); row->mouseDrag(rowEvent({ 100, 150 }, { 100, 118 }));
    presenter.dispatchPendingMessages();
    if (!expect(row->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "Escape handles track resize cancellation")) return false;
    presenter.dispatchPendingMessages();
    if (!expect(row->getHeight() == 120 && !session.gesturing(), "Escape restores the previous track height")) return false;
    auto* metrics = dynamic_cast<juce::Label*>(findTitled(editor, "Audio CPU load and peak UI lag"));
    if (!expect(metrics && metrics->getText().contains("CPU") && metrics->getText().contains("UI lag")
                && metrics->getY() == 0 && metrics->getRight() > editor.getWidth() - 10,
                "CPU and UI lag are visible at the top right")) return false;
    auto* metronome = dynamic_cast<juce::Button*>(findTitled(editor, "Toggle metronome;"));
    if (!expect(metronome && metronome->isShowing(), "metronome button is visible")) return false;
    metronome->setToggleState(true, juce::dontSendNotification); metronome->onClick();
    if (!expect(presenter.metronome(), "metronome button enables audio click")) return false;
    metronome->setToggleState(false, juce::dontSendNotification); metronome->onClick();
    if (!expect(!presenter.metronome(), "metronome button disables audio click")) return false;
    presenter.selectTime({ 1.5, 2, { project.tracks[1].id, project.tracks[2].id } }); presenter.dispatchPendingMessages();
    const auto perfAsset = newId(), perfTrack = newId(), perfClip = newId();
    const auto perfPath = "Audio/" + perfAsset + ".wav";
    std::unique_ptr<juce::OutputStream> output = session.mediaDirectory().getChildFile(perfPath).createOutputStream();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(output, juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(16));
    if (!expect(writer != nullptr, "long waveform fixture writer")) return false;
    juce::AudioBuffer<float> signal(2, 1024);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 1024; ++sample) signal.setSample(channel, sample, static_cast<float>(std::sin(sample * 0.013) * 0.25));
    for (int block = 0; block < 5625; ++block)
        if (!expect(writer->writeFromAudioSampleBuffer(signal, 0, 1024), "write long waveform fixture")) return false;
    writer.reset();
    Project longProject; longProject.tracks = { { perfTrack, "Long audio" } };
    longProject.assets = { { perfAsset, perfPath, 48000, 2, 5760000, false } };
    longProject.clips = { { perfClip, perfTrack, perfAsset, 0, 0, 120 } };
    session.replace(longProject, session.mediaDirectory(), true); presenter.setZoom(400); presenter.selectClips({});
    session.dispatchPendingMessages(); presenter.dispatchPendingMessages();
    session.splitTimeSelection({ 0.5, 1, { perfTrack } });
    session.dispatchPendingMessages(); presenter.dispatchPendingMessages();
    canvas->getProperties().set("waveform-renders", 0); canvas->getProperties().set("waveform-max-width", 0);
    editor.createComponentSnapshot(editor.getLocalBounds());
    const auto rendered = static_cast<int>(canvas->getProperties()["waveform-renders"]);
    const auto* viewport = canvas->findParentComponentOfClass<juce::Viewport>();
    if (!expect(rendered == 3 && viewport && static_cast<int>(canvas->getProperties()["waveform-max-width"]) <= viewport->getViewWidth(),
                "sliced long waveforms render only the visible width")) return false;
    for (int frame = 0; frame < 5; ++frame) editor.createComponentSnapshot(editor.getLocalBounds());
    if (!expect(static_cast<int>(canvas->getProperties()["waveform-renders"]) == rendered,
                "repeated sliced redraws reuse waveform images")) return false;
    canvas->mouseMagnify(event({ 100, 80 }, { 100, 80 }), 0.5f);
    editor.createComponentSnapshot(editor.getLocalBounds());
    const auto zoomed = static_cast<int>(canvas->getProperties()["waveform-renders"]);
    if (!expect(zoomed > rendered, "zoom invalidates waveform geometry")) return false;
    editor.createComponentSnapshot(editor.getLocalBounds());
    if (!expect(static_cast<int>(canvas->getProperties()["waveform-renders"]) == zoomed, "zoomed waveforms remain cached")) return false;
    canvas->mouseMagnify(event({ 100, 80 }, { 100, 80 }), 1000);
    editor.createComponentSnapshot(editor.getLocalBounds());
    const auto maximumZoomRenders = static_cast<int>(canvas->getProperties()["waveform-renders"]);
    if (!expect(std::abs(presenter.viewState().pixelsPerSecond - 40000) < 1.0e-9 && maximumZoomRenders > zoomed
                && static_cast<int>(canvas->getProperties()["waveform-max-width"]) <= viewport->getViewWidth(),
                "pinch reaches expanded zoom with bounded waveforms")) return false;
    editor.createComponentSnapshot(editor.getLocalBounds());
    if (!expect(static_cast<int>(canvas->getProperties()["waveform-renders"]) == maximumZoomRenders,
                "maximum zoom reuses waveform images")) return false;
    juce::Thread::sleep(1100); juce::Timer::callPendingTimersSynchronously();
    if (!expect(metrics->getText().fromLastOccurrenceOf("UI lag", false, false).getIntValue() >= 1000
                && metrics->findColour(juce::Label::textColourId) == juce::Colour(0xffa23b32),
                "UI lag detects and highlights a blocked message thread")) return false;
    presenter.setZoom(64); presenter.setScroll(0, 0); presenter.selectClips({});
    auto* addMidi = dynamic_cast<juce::Button*>(findTitled(editor, "Add a MIDI track"));
    if (!expect(addMidi != nullptr, "MIDI track button is present")) return false;
    addMidi->onClick(); session.dispatchPendingMessages(); presenter.dispatchPendingMessages();
    const auto midiTrack = session.project().tracks.back().id;
    if (!expect(session.project().tracks.back().midi, "MIDI track button creates a MIDI track")) return false;
    auto* addInstrument = dynamic_cast<juce::Button*>(findTitled(editor, "Add instrument in bottom panel"));
    if (!expect(addInstrument && addInstrument->isShowing(), "bottom instrument panel is visible")) return false;
    addInstrument->onClick();
    auto* picker = findTitled(editor, "Instrument search dropdown");
    auto* search = picker ? dynamic_cast<juce::TextEditor*>(findTitled(*picker, "Dropdown search")) : nullptr;
    juce::ListBox* choices = nullptr;
    if (picker) for (auto* child : picker->getChildren()) if (auto* rows = dynamic_cast<juce::ListBox*>(child)) choices = rows;
    if (!expect(picker && picker->isShowing() && search && choices, "instrument dropdown remains open after click")) return false;
    session.dispatchPendingMessages(); presenter.dispatchPendingMessages(); juce::Timer::callPendingTimersSynchronously();
    if (!expect(picker->isShowing(), "refresh does not immediately close the dropdown")) return false;
    search->setText("missing", false); search->onTextChange();
    if (!expect(choices->getListBoxModel()->getNumRows() == 0 && picker->isShowing(), "instrument search filters unmatched names")) return false;
    search->setText("sAm", false); search->onTextChange();
    if (!expect(choices->getListBoxModel()->getNumRows() == 1, "instrument search filters dynamically and ignores case")) return false;
    search->onReturnKey();
    if (!expect(!picker->isShowing() && session.project().tracks.back().sampler, "choosing Sampler installs the bottom instrument")) return false;
    const auto sampleClip = session.project().clips[0];
    auto* drop = dynamic_cast<juce::DragAndDropTarget*>(canvas);
    drop->itemDropped({ "asset:" + sampleClip.asset, canvas, { 10, 120 } });
    if (!expect(findId(session.project().tracks, midiTrack)->sample == sampleClip.asset,
                "audio browser drop loads MIDI sampler")) return false;
    canvas->mouseDoubleClick(event({ 40, 120 }, { 40, 120 }));
    session.dispatchPendingMessages(); presenter.dispatchPendingMessages();
    auto* midiEditor = findTitled(editor, "Sampler and MIDI piano roll");
    if (!expect(midiEditor && midiEditor->isShowing() && midiEditor->getBottom() > editor.getHeight() - 40,
                "double click opens docked MIDI piano roll")) return false;
    auto* samplerDrop = dynamic_cast<juce::DragAndDropTarget*>(midiEditor);
    samplerDrop->itemDropped({ "clip:" + sampleClip.id, canvas, { 10, 10 } });
    const auto* samplerTrack = findId(session.project().tracks, midiTrack);
    if (!expect(samplerTrack->sample == sampleClip.asset && std::abs(samplerTrack->sampleDuration - sampleClip.duration) < 1.0e-9
                && std::abs(samplerTrack->sampleOffset - sampleClip.offset) < 1.0e-9,
                "clip drop loads only the trimmed source region")) return false;
    auto* sourceDropdown = dynamic_cast<juce::Button*>(findTitled(*midiEditor, "Choose audio clip for sampler"));
    sourceDropdown->onClick();
    if (!expect(picker->isShowing() && picker->getTitle() == "Audio clip search dropdown", "audio clip dropdown stays open")) return false;
    presenter.dispatchPendingMessages();
    if (!expect(picker->isShowing(), "audio clip dropdown survives view refresh")) return false;
    search->onEscapeKey();
    const auto patternId = session.project().clips.back().id;
    auto* roll = findTitled(*midiEditor, "MIDI piano roll;");
    if (!expect(roll != nullptr, "piano roll is present")) return false;
    const auto rollEvent = [roll](juce::Point<float> point, juce::Point<float> down, bool right = false, bool shift = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys((right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier)
                | (shift ? juce::ModifierKeys::shiftModifier : 0)),
            1, 0, 0, 0, 0, roll, roll, now, down, now, 1, point != down);
    };
    roll->mouseDown(rollEvent({ 100, 940 }, { 100, 940 })); roll->mouseUp(rollEvent({ 100, 940 }, { 100, 940 }));
    if (!expect(findId(session.project().clips, patternId)->notes.empty(), "single click selects without adding a note")) return false;
    roll->mouseDoubleClick(rollEvent({ 100, 940 }, { 100, 940 }));
    if (!expect(findId(session.project().clips, patternId)->notes.size() == 1, "piano roll double click adds a note")) return false;
    session.undoEdit(); if (!expect(findId(session.project().clips, patternId)->notes.empty(), "note insertion undoes")) return false;
    session.redoEdit();
    roll->mouseDown(rollEvent({ 108, 940 }, { 108, 940 })); roll->mouseDrag(rollEvent({ 153, 926 }, { 108, 940 }));
    roll->mouseUp(rollEvent({ 153, 926 }, { 108, 940 }));
    if (!expect(findId(session.project().clips, patternId)->notes[0].pitch == 61, "piano roll drag moves pitch")) return false;
    roll->mouseDown(rollEvent({ 162, 926 }, { 162, 926 })); roll->mouseDrag(rollEvent({ 229.5f, 926 }, { 162, 926 }));
    roll->mouseUp(rollEvent({ 229.5f, 926 }, { 162, 926 }));
    if (!expect(std::abs(findId(session.project().clips, patternId)->notes[0].duration - 0.5) < 1.0e-9,
                "piano roll edge drag resizes duration")) return false;
    roll->mouseMove(rollEvent({ 144, 926 }, { 144, 926 })); const auto leftNoteCursor = roll->getMouseCursor();
    roll->mouseMove(rollEvent({ 230, 926 }, { 230, 926 })); const auto rightNoteCursor = roll->getMouseCursor();
    if (!expect(leftNoteCursor != juce::MouseCursor::NormalCursor && rightNoteCursor != juce::MouseCursor::NormalCursor
                && leftNoteCursor != rightNoteCursor, "note edges show distinct bracket cursors")) return false;
    roll->mouseDown(rollEvent({ 144, 926 }, { 144, 926 })); roll->mouseDrag(rollEvent({ 121.5f, 926 }, { 144, 926 }));
    roll->mouseUp(rollEvent({ 121.5f, 926 }, { 144, 926 }));
    if (!expect(std::abs(findId(session.project().clips, patternId)->notes[0].start - 0.375) < 1.0e-9
                && std::abs(findId(session.project().clips, patternId)->notes[0].duration - 0.625) < 1.0e-9,
                "left note edge stretches while keeping the end fixed")) return false;
    const auto resizedNotes = findId(session.project().clips, patternId)->notes;
    auto* waveform = findTitled(*midiEditor, "Sampler waveform;");
    if (!expect(waveform && waveform->isShowing() && waveform->getWidth() > 200 && waveform->getHeight() > 60,
                "sampler has a visible waveform")) return false;
    auto* sampleStart = dynamic_cast<juce::Slider*>(findTitled(*midiEditor, "Sampler sample start"));
    auto* sampleEnd = dynamic_cast<juce::Slider*>(findTitled(*midiEditor, "Sampler sample end"));
    auto* samplerVolume = dynamic_cast<juce::Slider*>(findTitled(*midiEditor, "Sampler volume;"));
    if (!expect(sampleStart && sampleEnd && samplerVolume, "sampler range and volume controls are present")) return false;
    sampleStart->setValue(0.1, juce::sendNotificationSync);
    if (!expect(std::abs(findId(session.project().tracks, midiTrack)->sampleOffset - 0.1) < 1.0e-9,
                "sampler start control edits source range")) return false;
    session.undoEdit();
    session.dispatchPendingMessages(); presenter.dispatchPendingMessages();
    {
        auto imageOutput = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("midi-smoke.png").createOutputStream();
        if (imageOutput) { imageOutput->setPosition(0); imageOutput->truncate(); }
        juce::PNGImageFormat png;
        if (!expect(imageOutput && png.writeImageToStream(midiEditor->createComponentSnapshot(midiEditor->getLocalBounds()), *imageOutput),
                    "sampler and piano roll snapshot")) return false;
    }
    roll->mouseDown(rollEvent({ 147, 926 }, { 147, 926 })); roll->mouseDrag(rollEvent({ 192, 912 }, { 147, 926 }));
    if (!expect(roll->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))
                && findId(session.project().clips, patternId)->notes == resizedNotes,
                "Escape restores MIDI note drag")) return false;
    roll->mouseDown(rollEvent({ 147, 926 }, { 147, 926 }, true));
    if (!expect(findId(session.project().clips, patternId)->notes.empty(), "right click deletes MIDI note")) return false;
    roll->mouseDoubleClick(rollEvent({ 100, 940 }, { 100, 940 }));
    roll->mouseDoubleClick(rollEvent({ 190, 912 }, { 190, 912 }));
    roll->mouseDown(rollEvent({ 108, 940 }, { 108, 940 })); roll->mouseUp(rollEvent({ 108, 940 }, { 108, 940 }));
    roll->mouseDown(rollEvent({ 198, 912 }, { 198, 912 }, false, true)); roll->mouseUp(rollEvent({ 198, 912 }, { 198, 912 }, false, true));
    roll->mouseDown(rollEvent({ 108, 940 }, { 108, 940 })); roll->mouseDrag(rollEvent({ 130.5f, 926 }, { 108, 940 }));
    roll->mouseUp(rollEvent({ 130.5f, 926 }, { 108, 940 }));
    const auto groupNotes = findId(session.project().clips, patternId)->notes;
    if (!expect(groupNotes.size() == 2 && groupNotes[0].pitch == 61 && groupNotes[1].pitch == 63
                && std::abs(groupNotes[0].start - 0.375) < 1.0e-9 && std::abs(groupNotes[1].start - 0.875) < 1.0e-9,
                "Shift-selected notes move together")) return false;
    roll->mouseDown(rollEvent({ 140, 926 }, { 140, 926 })); roll->mouseDrag(rollEvent({ 162.5f, 926 }, { 140, 926 }));
    roll->mouseUp(rollEvent({ 162.5f, 926 }, { 140, 926 }));
    if (!expect(std::abs(findId(session.project().clips, patternId)->notes[0].duration - 0.25) < 1.0e-9
                && std::abs(findId(session.project().clips, patternId)->notes[1].duration - 0.25) < 1.0e-9,
                "selected note edges stretch together")) return false;
    session.undoEdit();
    session.undoEdit();
    roll->mouseDown(rollEvent({ 85, 896 }, { 85, 896 })); roll->mouseDrag(rollEvent({ 220, 954 }, { 85, 896 }));
    roll->mouseUp(rollEvent({ 220, 954 }, { 85, 896 }));
    roll->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    if (!expect(findId(session.project().clips, patternId)->notes.empty(), "drag selection box selects multiple notes for deletion")) return false;
    session.undoEdit();
    roll->mouseDown(rollEvent({ 108, 940 }, { 108, 940 })); roll->mouseUp(rollEvent({ 108, 940 }, { 108, 940 }));
    roll->mouseDown(rollEvent({ 108, 940 }, { 108, 940 })); roll->mouseDrag(rollEvent({ 130.5f, 940 }, { 108, 940 }));
    roll->mouseUp(rollEvent({ 130.5f, 940 }, { 108, 940 }));
    if (!expect(std::abs(findId(session.project().clips, patternId)->notes[1].start - 0.75) < 1.0e-9,
                "single note selection leaves other notes unmoved")) return false;
    roll->mouseDown(rollEvent({ 20, 886 }, { 20, 886 }));
    const auto mouseKeyImage = roll->createComponentSnapshot({ 0, 0, 260, 1000 });
    if (!expect(mouseKeyImage.getPixelAt(30, 888) != juce::Colours::white, "mouse press highlights piano key")) return false;
    roll->mouseUp(rollEvent({ 20, 886 }, { 20, 886 }));
    const auto keyboardRevision = session.snapshot().revision;
    editor.grabKeyboardFocus();
    if (!expect(editor.keyPressed(juce::KeyPress('z')) && editor.keyPressed(juce::KeyPress('q')),
                "computer keyboard starts octaves with Z and Q")) return false;
    auto* midiRow = findNamed(editor, "MIDI track");
    auto* midiName = midiRow ? dynamic_cast<juce::Label*>(findTitled(*midiRow, "Track name;")) : nullptr;
    if (!expect(midiName != nullptr, "MIDI track colour is present")) return false;
    const auto noteColour = midiName->findColour(juce::Label::backgroundColourId);
    const auto keyboardImage = roll->createComponentSnapshot({ 0, 0, 260, 1000 });
    if (!expect(keyboardImage.getPixelAt(30, 776) == noteColour && keyboardImage.getPixelAt(130, 944) == noteColour,
                "keyboard press and piano notes use track colour")) return false;
    editor.keyStateChanged(false);
    const auto releasedKeyboard = roll->createComponentSnapshot({ 0, 0, 260, 1000 });
    if (!expect(releasedKeyboard.getPixelAt(30, 776) == juce::Colours::white, "keyboard key highlight clears on release")) return false;
    if (!expect(session.snapshot().revision == keyboardRevision, "piano keyboard does not edit the project")) return false;
    auto* audioName = dynamic_cast<juce::Label*>(findTitled(editor, "Track name;"));
    const auto nameEvent = [audioName]
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { 5, 5 },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, audioName, audioName, now, { 5, 5 }, now, 1, false);
    };
    audioName->getParentComponent()->mouseDown(nameEvent()); presenter.dispatchPendingMessages();
    if (!expect(presenter.viewState().selectedTrack == session.project().tracks[0].id
                && audioName->findColour(juce::Label::outlineColourId).getAlpha() > 0,
                "track name click selects and highlights the track")) return false;
    presenter.selectTrack(midiTrack); presenter.dispatchPendingMessages();
    if (!expect(editor.keyPressed(juce::KeyPress('`')) && !presenter.viewState().pianoOpen,
                "tilde key closes piano roll")) return false;
    canvas->mouseDoubleClick(event({ 45, 130 }, { 45, 130 }));
    if (!expect(!presenter.viewState().pianoOpen, "MIDI clip body double click leaves roll closed")) return false;
    canvas->mouseDoubleClick(event({ 45, 110 }, { 45, 110 }));
    if (!expect(presenter.viewState().pianoOpen, "MIDI clip header double click opens roll")) return false;
    auto* grip = findTitled(editor, "Piano roll top edge;");
    if (!expect(grip && grip->isShowing(), "piano roll resize edge is visible")) return false;
    const auto gripEvent = [grip](float y)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { 20, y },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, grip, grip, now, { 20, 3 }, now, 1, std::abs(y - 3) > 0.01f);
    };
    const auto initialPianoHeight = presenter.viewState().pianoHeight;
    grip->mouseDown(gripEvent(3)); grip->mouseDrag(gripEvent(43)); grip->mouseUp(gripEvent(43));
    if (!expect(presenter.viewState().pianoHeight == initialPianoHeight - 40, "top edge resizes piano roll vertically")) return false;
    grip->mouseDown(gripEvent(3)); grip->mouseDrag(gripEvent(-10000)); grip->mouseUp(gripEvent(-10000));
    if (!expect(grip->getY() <= 70 && midiEditor->getY() <= 80, "piano roll expands to the app header")) return false;
    grip->mouseDown(gripEvent(3)); grip->mouseDrag(gripEvent(20000)); grip->mouseUp(gripEvent(20000));
    if (!expect(!presenter.viewState().pianoOpen && midiEditor->isShowing(), "dragging fully down closes only the piano roll")) return false;
    if (!expect(editor.keyPressed(juce::KeyPress('~')) && presenter.viewState().pianoOpen, "tilde reopens selected MIDI clip")) return false;
    text->grabKeyboardFocus();
    if (!expect(!editor.keyPressed(juce::KeyPress('q')) && !editor.keyPressed(juce::KeyPress('~')),
                "piano shortcuts preserve text entry")) return false;
    editor.grabKeyboardFocus();
    juce::Logger::writeToLog("Timeline checks passed: selection, clipboard, zoom, track resizing, waveform cache, UI lag, MIDI notes and sampler drops.");
    return true;
}
}
