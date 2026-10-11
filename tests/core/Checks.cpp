// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/AudioEngine.h"
#include "domain/ProjectSession.h"
#include "application/EditorPresenter.h"
#include "infrastructure/Storage.h"
#include "domain/TimelineMath.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <chrono>
#include <cstdlib>

thread_local bool inAudioCallback = false;
std::atomic<int> callbackAllocations { 0 }, callbackFrees { 0 };
void* operator new(std::size_t size)
{
    if (inAudioCallback || opentone::AudioEngine::isRendering()) ++callbackAllocations;
    if (auto* pointer = std::malloc(std::max(size, std::size_t(1)))) return pointer;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { if ((inAudioCallback || opentone::AudioEngine::isRendering()) && pointer) ++callbackFrees; std::free(pointer); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }

namespace
{
using namespace opentone;
void check(bool success, const char* message) { if (!success) throw std::runtime_error(message); }
void near(double actual, double expected, double tolerance, const char* message)
{ if (std::abs(actual - expected) > tolerance) { std::cerr << actual << " != " << expected << '\n'; check(false, message); } }
const Clip& startingClip(const Project& project, const juce::String& track, double start)
{
    const auto found = std::find_if(project.clips.begin(), project.clips.end(), [&](const auto& clip)
    { return clip.track == track && std::abs(clip.start - start) < 1.0e-9; });
    check(found != project.clips.end(), "clip exists at the expected start"); return *found;
}
struct Workspace
{
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Opentone-check-" + newId());
    Workspace() { check(root.createDirectory().wasOk(), "create test directory"); }
    ~Workspace() { root.deleteRecursively(); }
};
juce::File fixture(const juce::File& directory, double rate, int channels, int seconds = 4, bool constant = false, const char* extension = "wav")
{
    const auto file = directory.getChildFile(newId() + "." + extension);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    auto* format = formats.findFormatForFileExtension(extension); check(format != nullptr, "fixture format");
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream(); check(stream != nullptr, "create fixture");
    auto writer = format->createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(channels).withBitsPerSample(24));
    check(writer != nullptr, "fixture writer");
    juce::AudioBuffer<float> buffer(channels, 1024);
    const auto total = static_cast<int>(rate * seconds);
    for (int frame = 0; frame < total; frame += 1024)
    {
        const int count = std::min(1024, total - frame);
        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < count; ++sample)
                buffer.setSample(channel, sample, constant ? (channel == 0 ? 0.125f : 0.25f)
                    : (channel == 0 ? 0.1f : 0.2f) + static_cast<float>((frame + sample) % 48000) / (channel == 0 ? 200000.0f : 100000.0f));
        check(writer->writeFromAudioSampleBuffer(buffer, 0, count), "write fixture");
    }
    return file;
}
void render(AudioEngine& engine, juce::AudioBuffer<float>& buffer)
{
    inAudioCallback = true;
    engine.render(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
    inAudioCallback = false;
}
void awaitRevision(AudioEngine& engine, uint64_t revision, juce::AudioBuffer<float>& buffer)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (engine.appliedRevision() != revision && std::chrono::steady_clock::now() < deadline)
    { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    check(engine.appliedRevision() == revision, "revision eventually adopted");
}
void modelChecks()
{
    ProjectSession session;
    near(session.project().bpm, 120, 0, "default BPM");
    check(session.project().tracks.empty() && !session.dirty(), "empty defaults");
    const auto track = session.addTrack("One");
    Asset asset { newId(), "Audio/test.wav", 48000, 2, 480000, false };
    Clip clip { newId(), track, asset.id, 2, 1, 3 };
    check(session.importAsset(asset, clip), "import operation");
    const auto saved = session.snapshot();
    check(session.setBpm(90), "set BPM"); near(session.project().clips[0].start, 2, 0, "BPM keeps timing");
    session.saved(saved, saved.directory); check(session.dirty(), "old save preserves newer dirty edits");
    check(!session.setBpm(std::nan("")), "reject NaN BPM");
    auto unchangedRevision = session.snapshot().revision;
    session.beginGesture(); session.endGesture(false);
    check(session.snapshot().revision == unchangedRevision, "selection click is not an edit");
    check(!session.controls(track, -1, false), "reject negative gain");
    auto invalid = clip; invalid.offset = 9;
    check(!session.changeClip(invalid), "reject trim beyond source");
    invalid = clip; invalid.track = "missing"; check(!session.changeClip(invalid), "reject references");
    invalid = clip; invalid.start = std::numeric_limits<double>::infinity(); check(!session.changeClip(invalid), "reject infinite timing");
    auto moved = clip; moved.start = 5;
    session.beginGesture(); check(session.changeClip(moved), "drag preview");
    const auto revision = session.snapshot().revision;
    session.endGesture(true); near(session.project().clips[0].start, 2, 0, "escape restores drag");
    check(session.snapshot().revision > revision, "cancel progresses revision");
    session.beginGesture(); session.changeClip(moved); moved.start = 6; session.changeClip(moved); session.endGesture(false);
    session.undoEdit(); near(session.project().clips[0].start, 2, 0, "gesture one undo");
    session.redoEdit(); near(session.project().clips[0].start, 6, 0, "gesture redo");
    check(session.deleteTrack(track), "delete track"); check(session.project().clips.empty(), "track removes clips");
    session.undoEdit(); check(session.project().tracks[0].id == track && session.project().clips[0].id == clip.id, "undo restores IDs");
    auto generation = session.snapshot().generation; session.replace({}, {}, true);
    check(session.snapshot().generation != generation && !session.dirty(), "replace generation");
    session.saved(saved, juce::File::getSpecialLocation(juce::File::tempDirectory));
    check(session.location() == juce::File(), "stale save cannot switch location");
    session.undoEdit(); check(session.project().tracks.empty(), "new resets undo");
    near(snapTime(0.76, 120, true), 1, 0, "quarter snap");
    near(snapTime(0.25, 120, true), 0.5, 0, "halfway snap rounds forward");
    near(snapTime(-1, 90, false), 0, 0, "clamp zero");
    near(timelineGridStep(120, 64), 0.5, 0, "normal zoom uses quarter-note grid");
    near(timelineGridStep(120, 128), 0.25, 0, "zooming in halves grid spacing");
    near(timelineGridStep(120, 400), 0.0625, 0, "high zoom uses finer grid");
    near(timelineGridStep(120, 40000), 0.00048828125, 0, "maximum zoom uses finer grid");
    near(timelineGridStep(120, 8), 2, 0, "zooming out uses a bar grid");
    near(snapToGrid(0.13, timelineGridStep(120, 400), true), 0.125, 0, "edits snap to fine grid lines");
    near(snapToGrid(0.13, timelineGridStep(120, 400), false), 0.13, 0, "snap off keeps continuous timing");
    auto trimmed = trimLeft(clip, 0); near(trimmed.start, 1, 0, "left trim source bound"); near(trimmed.offset, 0, 0, "left offset");
    near(trimmed.duration, 4, 0, "left duration"); near(trimRight(clip, 100, 10).duration, 9, 0, "right bound");
    ProjectSession group;
    const auto groupTrack = group.addTrack();
    auto firstClip = clip; firstClip.track = groupTrack;
    check(group.importAsset(asset, firstClip), "group first clip");
    auto secondClip = firstClip; secondClip.id = newId(); secondClip.start = 6;
    check(group.insertClip(secondClip), "group second clip");
    const auto beforeGroup = group.project();
    auto firstMoved = firstClip, secondMoved = secondClip; ++firstMoved.start; ++secondMoved.start;
    auto invalidMoved = secondMoved; invalidMoved.offset = 100;
    check(!group.changeClips({ firstMoved, invalidMoved }) && group.project() == beforeGroup,
          "invalid batch move changes no clips");
    group.beginGesture(); check(group.changeClips({ firstMoved, secondMoved }), "group drag preview");
    group.endGesture(true); check(group.project() == beforeGroup, "cancel restores the entire group");
    group.beginGesture(); group.changeClips({ firstMoved, secondMoved }); group.endGesture(false);
    group.undoEdit(); check(group.project() == beforeGroup, "group move is one undo action");
    group.redoEdit(); near(group.project().clips[1].start - group.project().clips[0].start, 4, 0, "group preserves spacing");
    const auto beforeDelete = group.project();
    check(!group.deleteClips({ firstClip.id, "missing" }) && group.project() == beforeDelete,
          "invalid batch deletion changes no clips");
    check(group.deleteClips({ firstClip.id, secondClip.id }) && group.project().clips.empty(), "delete group");
    group.undoEdit(); check(group.project() == beforeDelete && group.project().assets.size() == 1,
          "group delete is one undo action and retains audio");
    ProjectSession slicing; slicing.replace(beforeGroup, {}, true);
    const TimeSelection range { 3, 4, { groupTrack } };
    const auto sliced = slicing.splitTimeSelection(range);
    check(sliced.size() == 1 && sliced[0].id == firstClip.id && slicing.project().clips.size() == 4,
          "time range isolates a partial clip with stable middle ID");
    near(sliced[0].offset, 2, 0, "slice advances the source offset");
    near(sliced[0].duration, 1, 0, "slice duration");
    near(slicing.project().clips[0].duration, 1, 0, "left remainder duration");
    near(slicing.project().clips[2].offset, 3, 0, "right remainder source offset");
    slicing.undoEdit(); check(slicing.project() == beforeGroup, "one undo restores an unsplit clip");
    slicing.beginGesture(); auto parts = slicing.splitTimeSelection(range); parts[0].start = 8;
    slicing.changeClips(parts); slicing.endGesture(true);
    check(slicing.project() == beforeGroup, "cancel restores split and movement together");
    check(slicing.splitTimeSelection({ 3, 4, { "missing" } }).empty()
          && slicing.splitTimeSelection({ 4, 3, { groupTrack } }).empty()
          && slicing.splitTimeSelection({ std::nan(""), 4, { groupTrack } }).empty()
          && slicing.project() == beforeGroup, "invalid time ranges change nothing");
    check(slicing.splitTimeSelection({ 5, 6, { groupTrack } }).empty(), "touching clip edges select no audio");
    std::cout << "model / gestures / timeline / partial clips: passed\n";
}
void storageChecks()
{
    Workspace workspace; const auto source = fixture(workspace.root, 48000, 2);
    const auto projectDirectory = workspace.root.getChildFile("project");
    auto imported = Storage::copyAudio(source, projectDirectory); check(imported.result.wasOk(), "copy import");
    auto duplicate = Storage::copyAudio(source, projectDirectory); check(duplicate.result.wasOk(), "duplicate import");
    check(imported.asset.path != duplicate.asset.path, "unique filenames");
    Snapshot snapshot; snapshot.directory = projectDirectory; snapshot.generation = 1; snapshot.revision = 1;
    snapshot.project.tracks.push_back({ newId(), "Track", 0.5, true });
    snapshot.project.assets = { imported.asset, duplicate.asset };
    snapshot.project.clips.push_back({ newId(), snapshot.project.tracks[0].id, imported.asset.id, 1.2, 0.2, 2.1 });
    check(Storage::write(snapshot, projectDirectory).wasOk(), "save");
    const auto moved = workspace.root.getChildFile("moved"); check(projectDirectory.moveFileTo(moved), "move project directory");
    auto reopened = Storage::load(moved); check(reopened.result.wasOk(), "portable load");
    near(reopened.project.clips[0].offset, 0.2, 0, "roundtrip source offset");
    check(reopened.project.tracks[0].mute && std::equal_to<double>()(reopened.project.assets[0].sampleRate, 48000), "metadata / controls");
    check(!Storage::safePath(moved, "../outside.wav") && !Storage::safePath(moved, "/etc/passwd"), "traversal");
    check(!Storage::safePath(moved, "Audio\\..\\outside.wav"), "Windows traversal");
    const auto unicodeDirectory = workspace.root.getChildFile(juce::String::fromUTF8("\xe9\x9f\xb3\xe5\xa3\xb0"));
    check(unicodeDirectory.createDirectory().wasOk() && Storage::safePath(unicodeDirectory, "Audio/test.wav"), "UTF-8 project path");
    const auto link = moved.getChildFile("Audio/escape");
    if (workspace.root.createSymbolicLink(link, true)) check(!Storage::safePath(moved, "Audio/escape/outside.wav"), "symlink escape");
    moved.getChildFile(imported.asset.path).deleteFile();
    auto missing = Storage::load(moved); check(missing.result.wasOk() && missing.project.assets[0].missing && !missing.warning.isEmpty(), "missing media placeholder");
    const auto manifest = moved.getChildFile("project.opentone"); auto original = manifest.loadFileAsString();
    manifest.replaceWithText(original.replace("\"version\": 2", "\"version\": 99")); check(Storage::load(moved).result.failed(), "unsupported version");
    manifest.replaceWithText(original.replace(imported.asset.path, "../outside.wav")); check(Storage::load(moved).result.failed(), "manifest traversal");
    manifest.replaceWithText("{broken"); check(Storage::load(moved).result.failed(), "malformed manifest"); manifest.replaceWithText(original);
    snapshot.directory = moved;
    check(Storage::write(snapshot, moved).failed(), "failed missing asset copy"); check(manifest.loadFileAsString() == original, "failed save preserves manifest");
    const auto blocked = workspace.root.getChildFile("blocked"); blocked.replaceWithText("file");
    check(Storage::write(snapshot, blocked).failed(), "failed destination");
    check(Storage::copyAudio(blocked, moved).result.failed(), "unsupported audio rejected");
    check(Storage::copyAudio(fixture(workspace.root, 48000, 3, 1), moved).result.failed(), "reject higher-channel audio");
    check(Storage::copyAudio(fixture(workspace.root, 44100, 1, 1, true, "aiff"), moved).result.wasOk(), "AIFF import");
    check(Storage::copyAudio(fixture(workspace.root, 32000, 2, 1, true, "flac"), moved).result.wasOk(), "FLAC import");
    auto replacementFailure = workspace.root.getChildFile("replacement-failure");
    auto existingManifest = replacementFailure.getChildFile("project.opentone"); existingManifest.createDirectory();
    auto sentinel = existingManifest.getChildFile("keep"); sentinel.replaceWithText("original");
    Snapshot empty; empty.directory = moved;
    check(Storage::write(empty, replacementFailure).failed(), "failed atomic manifest replacement");
    check(sentinel.loadFileAsString() == "original", "failed replacement preserves existing destination");
    const auto overwriteSource = workspace.root.getChildFile("overwrite-source");
    const auto overwriteTarget = workspace.root.getChildFile("overwrite-target");
    auto newAudio = Storage::copyAudio(source, overwriteSource);
    check(newAudio.result.wasOk(), "overwrite source fixture");
    const auto oldAudio = overwriteTarget.getChildFile(newAudio.asset.path);
    check(oldAudio.getParentDirectory().createDirectory().wasOk(), "overwrite destination directory");
    check(fixture(workspace.root, 48000, 2, 4, true).copyFileTo(oldAudio), "different destination audio");
    const auto backup = workspace.root.getChildFile("old-audio.wav");
    check(oldAudio.copyFileTo(backup), "backup destination fixture");
    const auto oldManifest = overwriteTarget.getChildFile("project.opentone");
    check(oldManifest.replaceWithText("original manifest"), "overwrite manifest fixture");
    Snapshot overwrite; overwrite.directory = overwriteSource;
    overwrite.project.assets = { newAudio.asset, { newId(), "Audio/gone.wav", 48000, 2, 1, false } };
    check(Storage::write(overwrite, overwriteTarget).failed(), "later media failure aborts save");
    check(oldAudio.hasIdenticalContentTo(backup) && oldManifest.loadFileAsString() == "original manifest",
          "failed save preserves destination audio and manifest");
    overwrite.project.assets.pop_back();
    oldManifest.deleteFile(); check(oldManifest.createDirectory().wasOk(), "block manifest replacement");
    check(Storage::write(overwrite, overwriteTarget).failed(), "manifest failure after media copy");
    check(oldAudio.hasIdenticalContentTo(backup), "manifest failure preserves destination audio");
    check(overwriteTarget.findChildFiles(juce::File::findFiles, true, "*.wav").size() == 1,
          "failed saves remove only their new media copies");
    oldManifest.deleteRecursively();
    check(Storage::write(overwrite, overwriteTarget).wasOk(), "save with conflicting media succeeds");
    auto overwritten = Storage::load(overwriteTarget);
    check(overwritten.result.wasOk() && overwritten.project.assets[0].id == newAudio.asset.id
          && overwritten.project.assets[0].path != newAudio.asset.path, "manifest references fresh media with stable asset ID");
    check(overwriteTarget.getChildFile(overwritten.project.assets[0].path)
              .hasIdenticalContentTo(overwriteSource.getChildFile(newAudio.asset.path))
          && oldAudio.hasIdenticalContentTo(backup), "successful save preserves old media and copies new audio");
    check(Storage::write(overwrite, overwriteTarget).wasOk(), "repeat save with remapped media");
    check(Storage::load(overwriteTarget).project.assets[0].path == overwritten.project.assets[0].path
          && overwriteTarget.findChildFiles(juce::File::findFiles, true, "*.wav").size() == 2,
          "repeat save reuses the existing media copy");
    ProjectSession session;
    session.replace(reopened.project, moved, true);
    auto captured = session.snapshot(); session.setBpm(80);
    auto destination = workspace.root.getChildFile("save-as");
    session.saved(captured, destination);
    check(session.dirty() && session.location() == destination && session.snapshot().directory == moved,
          "Save As preserves stable media root and newer dirty state");
    Storage async; bool published = false;
    async.importFile(source, async.workspace(), [&](ImportResult) { published = true; }); async.cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    check(!published, "cancelled import");
    async.importFile(source, async.workspace(), [&](ImportResult) { published = true; });
    for (int wait = 0; wait < 2000 && async.workspace().findChildFiles(juce::File::findFiles, true, "*.wav").isEmpty(); ++wait)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    async.cancel(); juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    check(!published, "queued completion rejected after cancellation");
    ProjectSession saving;
    saving.addTrack("Save race"); const auto saveSnapshot = saving.snapshot();
    bool saved = false;
    const auto savedDirectory = workspace.root.getChildFile("async-save");
    async.save(saveSnapshot, savedDirectory, [&](juce::Result result)
    { check(result.wasOk(), "async snapshot save"); saving.saved(saveSnapshot, savedDirectory); saved = true; });
    saving.setBpm(80);
    for (int wait = 0; wait < 2000 && !saved; ++wait) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    check(saved && saving.dirty(), "real background save leaves newer edit dirty");
    near(Storage::load(savedDirectory).project.bpm, 120, 0, "save writes captured version");
    std::cout << "storage / portability / validation / failed saves: passed\n";
}
void overlapChecks()
{
    Project base; base.tracks = { { "one", "One" }, { "two", "Two" } };
    base.assets = { { "a", "Audio/a.wav", 48000, 1, 4800000, false }, { "b", "Audio/b.wav", 48000, 1, 4800000, false } };
    base.clips = { { "A", "one", "a", 0, 4, 10 }, { "other", "two", "a", 0, 2, 10 } };
    ProjectSession session;
    const Clip middle { "B", "one", "b", 3, 1, 4 };
    session.replace(base, {}, true); check(session.insertClip(middle), "place clip over existing audio");
    near(startingClip(session.project(), "one", 0).duration, 3, 0, "covered middle keeps left audio");
    near(startingClip(session.project(), "one", 7).duration, 3, 0, "covered middle keeps right audio");
    near(startingClip(session.project(), "one", 7).offset, 11, 0, "right remainder retains its source position");
    check(*findId(session.project().clips, "B") == middle && *findId(session.project().clips, "other") == base.clips[1],
          "incoming clip and other tracks stay unchanged");
    const auto placed = session.project(); session.undoEdit(); check(session.project() == base, "overlap placement is one undo step");
    session.redoEdit(); check(session.project() == placed, "overlap redo retains split IDs");
    for (const auto bounds : std::vector<std::pair<double, double>> { { 0, 3 }, { 7, 6 }, { 0, 12 }, { 10, 2 } })
    {
        session.replace(base, {}, true); auto clip = middle; clip.start = bounds.first; clip.duration = bounds.second;
        check(session.insertClip(clip), "place at overlap boundary");
        if (std::equal_to<double>()(clip.start, 0) && clip.duration > 10) check(!findId(session.project().clips, "A"), "full cover removes A");
        else if (std::equal_to<double>()(clip.start, 0))
        { near(findId(session.project().clips, "A")->start, 3, 0, "left cover trims A start"); near(findId(session.project().clips, "A")->offset, 7, 0, "left cover advances source offset"); }
        else if (clip.start < 10) near(findId(session.project().clips, "A")->duration, 7, 0, "right cover trims A end");
        else check(*findId(session.project().clips, "A") == base.clips[0], "touching edges do not cut audio");
        session.undoEdit(); check(session.project() == base, "boundary placement undo");
    }
    auto withMoving = base; auto moving = middle; moving.start = 20; moving.duration = 2; withMoving.clips.push_back(moving);
    session.replace(withMoving, {}, true); session.beginGesture(); moving.start = 3; session.changeClip(moving);
    check(*findId(session.project().clips, "A") == base.clips[0], "drag preview preserves covered clips");
    moving.start = 12; session.changeClip(moving); session.endGesture(false);
    check(*findId(session.project().clips, "A") == base.clips[0], "passing over a clip does not cut it");
    session.undoEdit(); session.beginGesture(); moving.start = 3; session.changeClip(moving); session.endGesture(true);
    check(session.project() == withMoving, "Escape restores all audio under a drag");
    session.beginGesture(); session.changeClip(moving); session.endGesture(false);
    near(startingClip(session.project(), "one", 5).offset, 9, 0, "drop cuts only the final covered interval");
    session.undoEdit(); check(session.project() == withMoving, "drag and overwrite undo together");
    session.replace(base, {}, true);
    check(session.insertClips({ { "left", "one", "b", 3, 0, 1 }, { "right", "one", "b", 6, 0, 1 } }), "place multiple clips");
    near(startingClip(session.project(), "one", 4).duration, 2, 0, "placement preserves gaps between incoming clips");
    session.replace(base, {}, true); auto invalid = middle; invalid.duration = 1000;
    check(!session.insertClip(invalid) && session.project() == base, "invalid placement never cuts existing audio");
    Asset imported { "imported", "Audio/import.wav", 48000, 1, 4800000, false };
    auto importedClip = middle; importedClip.asset = imported.id;
    check(session.importAsset(imported, importedClip) && session.project().clips.size() == 4, "imports use overlap replacement");
    session.undoEdit(); check(session.project() == base, "import and overlap cuts undo together");
    std::cout << "overlap replacement / offsets / drag previews / undo: passed\n";
}
Snapshot audioProject(const juce::File& directory, const juce::File& file)
{
    auto imported = Storage::copyAudio(file, directory); check(imported.result.wasOk(), "audio fixture import");
    Snapshot snapshot; snapshot.directory = directory; snapshot.generation = 1; snapshot.revision = 1;
    snapshot.project.tracks.push_back({ "track", "Track" }); snapshot.project.assets.push_back(imported.asset); return snapshot;
}
void audioChecks()
{
    Workspace workspace;
    auto snapshot = audioProject(workspace.root.getChildFile("project"), fixture(workspace.root, 48000, 1));
    const auto asset = snapshot.project.assets[0].id;
    snapshot.project.clips = { { "clip", "track", asset, 512.0 / 48000, 128.0 / 48000, 1024.0 / 48000 },
                              { "overlap", "track", asset, 768.0 / 48000, 0, 512.0 / 48000 } };
    AudioEngine engine; engine.configureOffline(48000, 256); engine.replace(snapshot);
    juce::AudioBuffer<float> buffer(2, 256); awaitRevision(engine, 1, buffer); engine.play();
    render(engine, buffer); near(buffer.getMagnitude(0, 256), 0, 0, "gap block 1");
    render(engine, buffer); near(buffer.getMagnitude(0, 256), 0, 0, "gap block 2");
    render(engine, buffer); near(buffer.getSample(0, 0), 0.1 + 128.0 / 200000, 2.0e-6, "source offset");
    near(buffer.getSample(0, 100), buffer.getSample(1, 100), 0, "mono duplication");
    render(engine, buffer); near(buffer.getSample(0, 0), 0.2 + 384.0 / 200000, 2.0e-6, "additive overlap");
    render(engine, buffer); render(engine, buffer);
    check(engine.playing(), "playback continues past arrangement end"); near(engine.position(), 1536.0 / 48000, 1.0e-9, "end clock");
    engine.stop();
    engine.seek(0.012); awaitRevision(engine, 1, buffer);
    for (int i = 0; i < 200 && std::abs(engine.position() - 0.012) > 1.0e-9; ++i) { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    near(engine.position(), 0.012, 1.0e-9, "seek paused");
    snapshot.project.clips = { { "clip", "track", asset, 0, 0, 3 } }; ++snapshot.revision;
    engine.replace(snapshot); awaitRevision(engine, 2, buffer); engine.play();
    render(engine, buffer); engine.stop(); const auto stopped = engine.position(); render(engine, buffer);
    near(engine.position(), stopped, 0, "stop holds cursor"); engine.play(); render(engine, buffer); check(engine.position() > stopped, "resume advances");
    for (int i = 0; i < 50; ++i)
    { snapshot.project.clips[0].start = i * 0.001; ++snapshot.revision; engine.submit(snapshot); }
    const auto before = engine.position(); awaitRevision(engine, snapshot.revision, buffer);
    check(engine.position() > before, "live edits preserve clock");
    snapshot.project.tracks[0].mute = true; engine.submit(snapshot, false);
    render(engine, buffer); render(engine, buffer); near(buffer.getMagnitude(0, 256), 0, 1.0e-6, "smoothed mute");
    snapshot.project.clips.clear(); ++snapshot.revision; engine.submit(snapshot); awaitRevision(engine, snapshot.revision, buffer);
    check(engine.playing(), "shortening the arrangement does not stop playback");
    snapshot.generation = 2; snapshot.revision = 99; engine.replace(snapshot); awaitRevision(engine, 99, buffer);
    near(engine.position(), 0, 0, "replacement resets clock");
    engine.configureOffline(44100, 128); juce::AudioBuffer<float> small(2, 128); awaitRevision(engine, 99, small);
    near(engine.rate(), 44100, 0, "device reconfiguration");
    snapshot.generation = 3; snapshot.revision = 101;
    snapshot.project.tracks[0].mute = false;
    snapshot.project.clips = { { "inside-block", "track", asset, 550.0 / 48000, 0, 100.0 / 48000 } };
    engine.configureOffline(48000, 256); engine.replace(snapshot); awaitRevision(engine, 101, buffer); engine.play();
    render(engine, buffer); render(engine, buffer); render(engine, buffer);
    near(buffer.getSample(0, 37), 0, 0, "interior start excludes prior sample");
    near(buffer.getSample(0, 38), 0.1, 2.0e-6, "interior start includes first sample");
    check(buffer.getSample(0, 137) > 0, "interior end includes preceding sample");
    near(buffer.getSample(0, 138), 0, 0, "half-open interior end");
    near(engine.position(), 650.0 / 48000, 1.0e-9, "interior end clock");
    auto stereo = audioProject(workspace.root.getChildFile("stereo"), fixture(workspace.root, 24000, 2, 4, true));
    stereo.revision = 100; stereo.project.clips.push_back({ "stereo", "track", stereo.project.assets[0].id, 0, 0, 2 });
    engine.configureOffline(48000, 256); engine.replace(stereo); awaitRevision(engine, 100, buffer); engine.play();
    for (int i = 0; i < 4; ++i) render(engine, buffer);
    near(buffer.getSample(0, 128), 0.125, 1.0e-4, "resampled left"); near(buffer.getSample(1, 128), 0.25, 1.0e-4, "resampled right");
    auto resampled = audioProject(workspace.root.getChildFile("resampled-offset"), fixture(workspace.root, 24000, 2));
    resampled.revision = 110; resampled.project.clips.push_back({ "resampled", "track", resampled.project.assets[0].id, 0, 0.5, 2 });
    engine.replace(resampled); awaitRevision(engine, 110, buffer); engine.play();
    for (int i = 0; i < 4; ++i) render(engine, buffer);
    near(buffer.getSample(0, 128), 0.1 + (12000.0 + 448) / 200000, 1.0e-4, "resampling preserves source speed and trim");
    near(buffer.getSample(1, 128), 0.2 + (12000.0 + 448) / 100000, 1.0e-4, "resampling preserves stereo speed");
    auto continuous = audioProject(workspace.root.getChildFile("continuous"), fixture(workspace.root, 48000, 2));
    auto mono = Storage::copyAudio(fixture(workspace.root, 48000, 1), continuous.directory);
    check(mono.result.wasOk(), "second live fixture"); continuous.project.assets.push_back(mono.asset);
    continuous.project.tracks.push_back({ "moving", "Moving" }); continuous.revision = 201;
    continuous.project.clips = { { "unchanged", "track", continuous.project.assets[0].id, 0, 0, 3 },
                                 { "moved", "moving", mono.asset.id, 0, 0, 3 } };
    engine.replace(continuous); awaitRevision(engine, 201, buffer); engine.play(); render(engine, buffer); render(engine, buffer);
    for (int i = 0; i < 4; ++i)
    {
        continuous.project.clips[1].start = i % 2 ? 0.02 : 0.04; ++continuous.revision;
        engine.submit(continuous); awaitRevision(engine, continuous.revision, buffer);
        const auto frame = std::llround(engine.position() * 48000) - 256 + 100;
        near(buffer.getSample(1, 100) - buffer.getSample(0, 100), 0.1 + static_cast<double>(frame % 48000) / 200000,
             3.0e-6, "unchanged stereo voice preserves its source cursor during edits");
    }
    engine.pausePreparation.store(true);
    continuous.project.clips[1].start = 0.08; ++continuous.revision; engine.submit(continuous);
    const auto pendingCursor = engine.position(); const auto pendingUnderruns = engine.underrunCount();
    for (int i = 0; i < 40; ++i) { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    check(engine.position() > pendingCursor && engine.appliedRevision() != continuous.revision, "slow preparation keeps old playback progressing");
    check(engine.underrunCount() == pendingUnderruns, "slow preparation preserves active streams");
    engine.pausePreparation.store(false); awaitRevision(engine, continuous.revision, buffer);
    engine.stop(); engine.pauseCleanup.store(true);
    for (int i = 0; i < 24; ++i)
    {
        continuous.project.clips[1].start = i % 2 ? 0.02 : 0.04; ++continuous.revision; engine.submit(continuous);
        for (int wait = 0; wait < 40; ++wait) { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
    check(engine.ownedArrangements.load() <= 19, "saturated cleanup queue has bounded ownership");
    check(engine.appliedRevision() != continuous.revision, "saturated cleanup defers adoption");
    engine.pauseCleanup.store(false); awaitRevision(engine, continuous.revision, buffer);
    engine.pauseStreaming.store(true); engine.play();
    const auto initialUnderruns = engine.underrunCount();
    for (int i = 0; i < 70; ++i) render(engine, buffer);
    check(engine.underrunCount() > initialUnderruns, "source starvation counts underruns without waiting");
    engine.pauseStreaming.store(false);
    engine.seek(0.25); engine.stop();
    continuous.project.clips[1].start = 0.02; ++continuous.revision; engine.submit(continuous);
    awaitRevision(engine, continuous.revision, buffer);
    near(engine.position(), 0.25, 1.0e-9, "editing and stop during seek preserve requested position");
    check(!engine.playing(), "stop during seek prevents resume");
    for (int i = 0; i < 100; ++i) engine.seek(0.1 + i * 0.001);
    for (int wait = 0; wait < 2000 && std::abs(engine.position() - 0.199) > 1.0e-9; ++wait)
    { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    near(engine.position(), 0.199, 1.0e-9, "rapid seeks adopt latest position");
    ++continuous.revision; continuous.project.bpm = 90; engine.submit(continuous, false);
    engine.seek(0.1);
    awaitRevision(engine, continuous.revision, buffer);
    check(engine.requestedRevision() == engine.appliedRevision(), "seek requests the latest control revision");
    engine.play(); const auto seekCursor = engine.position();
    for (int i = 0; i < 40; ++i) { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    near(engine.position(), seekCursor + 40.0 * 256 / 48000, 1.0e-9, "seek after tempo change does not repeat");
    continuous.project.clips[1].start = 0.04; ++continuous.revision; engine.submit(continuous);
    awaitRevision(engine, continuous.revision, buffer);
    check(engine.playing(), "live edit after unaligned seek activates without stopping");
    engine.pausePreparation.store(true); engine.replace(continuous); engine.play();
    render(engine, buffer); near(engine.position(), 0, 0, "startup waits for first arrangement");
    ++continuous.revision; continuous.project.bpm = 100; engine.submit(continuous, false);
    engine.pausePreparation.store(false); awaitRevision(engine, continuous.revision, buffer);
    check(engine.playing() && engine.position() > 0 && buffer.getMagnitude(0, 256) > 0,
          "Play before preparation starts audible playback");
    check(engine.requestedRevision() == engine.appliedRevision(), "startup control changes finish preparation");
    check(callbackAllocations.load() == 0 && callbackFrees.load() == 0, "callback allocates / destroys nothing");
    std::cout << "audio / scheduling / mapping / mixed rates / live edits / callback ownership: passed\n";
}
void midiChecks()
{
    Workspace workspace;
    auto snapshot = audioProject(workspace.root.getChildFile("midi"), fixture(workspace.root, 48000, 1));
    auto& track = snapshot.project.tracks[0]; track.midi = true; track.sampler = true; track.name = "Sampler";
    track.sample = snapshot.project.assets[0].id; track.sampleOffset = 0.1; track.sampleDuration = 0.4;
    Clip pattern { "pattern", track.id, {}, 0, 0, 1.2 };
    pattern.notes = { { 60, 0.05, 0.2, 0.5 }, { 72, 0.4, 0.3, 1 }, { 48, 0.8, 0.2, 0.25 } };
    snapshot.project.clips = { pattern };
    check(validate(snapshot.project).wasOk(), "valid MIDI sampler project");
    auto invalid = snapshot.project; invalid.clips[0].notes[0].pitch = 128;
    check(validate(invalid).failed(), "MIDI pitch is validated");
    invalid = snapshot.project; invalid.tracks[0].sampleDuration = 100;
    check(validate(invalid).failed(), "sampler source range is validated");
    invalid = snapshot.project; invalid.clips[0].asset = track.sample;
    check(validate(invalid).failed(), "MIDI tracks reject audio clips");
    const auto saved = workspace.root.getChildFile("saved-midi");
    check(Storage::write(snapshot, saved).wasOk(), "save MIDI sampler project");
    const auto loaded = Storage::load(saved);
    check(loaded.result.wasOk() && loaded.project == snapshot.project, "MIDI notes and sampler round-trip");
    const auto legacy = workspace.root.getChildFile("legacy"); legacy.createDirectory();
    legacy.getChildFile("project.opentone").replaceWithText("{\"version\":1,\"bpm\":120,\"tracks\":[{\"id\":\"audio\",\"name\":\"Audio\",\"gain\":1,\"mute\":false}],\"assets\":[],\"clips\":[]}");
    check(Storage::load(legacy).result.wasOk() && !Storage::load(legacy).project.tracks[0].midi, "legacy audio projects still load");
    juce::var malformed; juce::JSON::parse(saved.getChildFile("project.opentone").loadFileAsString(), malformed);
    malformed["clips"].getArray()->getReference(0)["notes"].getArray()->getReference(0).getDynamicObject()->setProperty("velocity", "loud");
    saved.getChildFile("project.opentone").replaceWithText(juce::JSON::toString(malformed));
    check(Storage::load(saved).result.failed(), "malformed MIDI notes fail at load boundary");
    ProjectSession session; session.replace(snapshot.project, snapshot.directory, true);
    session.loadSampler(track.id, track.sample, 0.2, 0.3); session.undoEdit();
    check(session.project() == snapshot.project, "sampler changes undo");
    auto notes = pattern; notes.notes.push_back({ 64, 0.3, 0.1, 0.6 }); session.changeClip(notes); session.undoEdit();
    check(session.project() == snapshot.project, "note changes undo");
    auto parts = session.splitTimeSelection({ 0.1, 0.15, { track.id } });
    check(parts.size() == 1 && parts[0].notes == pattern.notes, "MIDI split preserves note source timing");
    near(parts[0].offset, 0.1, 1.0e-9, "MIDI split advances source offset"); session.undoEdit();
    AudioEngine engine; engine.configureOffline(48000, 256); engine.replace(snapshot);
    juce::AudioBuffer<float> buffer(2, 256); awaitRevision(engine, 1, buffer); engine.play();
    std::vector<float> audio;
    for (int block = 0; block < 230; ++block)
    { render(engine, buffer); audio.insert(audio.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 256);
      if (block == 19) check(engine.activeNotes(track.id).test(60) && !engine.activeNotes(track.id).test(72), "MIDI playback publishes pressed root key");
      if (block == 59) check(engine.activeNotes(track.id).none(), "MIDI gaps clear pressed keys");
      if (block == 89) check(engine.activeNotes(track.id).test(72), "MIDI playback publishes transposed key");
      std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    near(audio[1000], 0, 0, "MIDI note start is sample accurate");
    near(audio[4800], 0.068, 0.003, "root note uses trimmed source and velocity");
    near(audio[21600], 0.148, 0.003, "octave up plays source at twice speed");
    near(audio[31200], 0, 0, "pitched note stops at sample region end");
    near(audio[40800], 0.0325, 0.003, "octave down plays source at half speed");
    check(engine.playing(), "MIDI playback continues past clip end");
    engine.stop();
    engine.seek(0.45);
    for (int wait = 0; wait < 2000 && std::abs(engine.position() - 0.45) > 1.0e-9; ++wait)
    { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    near(engine.position(), 0.45, 1.0e-9, "MIDI seek reaches selected time");
    engine.play(); render(engine, buffer); render(engine, buffer);
    near(buffer.getSample(0, 200), 0.153, 0.005, "MIDI seek resumes inside pitched note");
    check(callbackAllocations.load() == 0 && callbackFrees.load() == 0, "sampler callback allocates / destroys nothing");
    engine.stop(); const auto pausedPosition = engine.position(); const auto revisionBeforePreview = engine.requestedRevision();
    const auto& sampleAsset = snapshot.project.assets[0]; const auto sampleFile = snapshot.directory.getChildFile(sampleAsset.path);
    check(engine.previewNote(track, sampleAsset, sampleFile, 60, 0.8, true), "paused keyboard note is accepted");
    float previewPeak = 0;
    for (int wait = 0; wait < 2000 && previewPeak < 0.05f; ++wait)
    { render(engine, buffer); previewPeak = buffer.getMagnitude(0, 256); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    check(previewPeak > 0.05f && !engine.playing(), "sampler auditions while paused");
    near(engine.position(), pausedPosition, 0, "audition leaves arrangement playhead untouched");
    check(engine.requestedRevision() == revisionBeforePreview, "audition does not submit arrangement revisions");
    check(engine.previewNote(track, sampleAsset, sampleFile, 72, 0.8, true), "second keyboard note is accepted");
    previewPeak = 0;
    for (int wait = 0; wait < 1000 && previewPeak < 0.15f; ++wait)
    { render(engine, buffer); previewPeak = buffer.getMagnitude(0, 256); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    check(previewPeak > 0.15f, "held keyboard notes form a chord");
    check(engine.activeNotes(track.id).test(60) && engine.activeNotes(track.id).test(72), "audition publishes both pressed keys");
    engine.releasePreview(track.id, -1);
    for (int block = 0; block < 4; ++block) { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    near(buffer.getMagnitude(0, 256), 0, 0, "keyboard release ends audition");
    check(engine.activeNotes(track.id).none(), "released audition keys clear");
    check(engine.previewNote(track, sampleAsset, sampleFile, 60, 0.8, false), "note edit audition is accepted");
    for (int block = 0; block < 300; ++block) { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    near(buffer.getMagnitude(0, 256), 0, 0, "edit audition ends without a key release");
    check(callbackAllocations.load() == 0 && callbackFrees.load() == 0, "audition callback allocates / destroys nothing");
    {
        auto silent = snapshot; silent.project.tracks[0].sampler = false; silent.project.tracks[0].sample.clear();
        silent.project.tracks[0].sampleOffset = silent.project.tracks[0].sampleDuration = 0;
        AudioEngine midiOnly; midiOnly.configureOffline(48000, 256); midiOnly.replace(silent);
        awaitRevision(midiOnly, 1, buffer); midiOnly.play();
        for (int block = 0; block < 20; ++block) render(midiOnly, buffer);
        check(midiOnly.activeNotes(track.id).test(60), "MIDI keys highlight without loaded sample");
        near(buffer.getMagnitude(0, 256), 0, 0, "MIDI without instrument remains silent");
    }
    Storage storage; EditorPresenter presenter(session, storage, engine);
    presenter.selectClips({ pattern.id }); presenter.copySelection(); presenter.duplicateSelection();
    check(session.project().clips.size() == 2 && session.project().clips.back().notes == pattern.notes,
          "MIDI duplication preserves notes"); session.undoEdit();
    const auto beforeImport = session.project();
    presenter.importFiles({ snapshot.directory.getChildFile(snapshot.project.assets[0].path).getFullPathName() }, track.id, 0);
    for (int wait = 0; wait < 2000 && presenter.busy(); ++wait) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    check(!presenter.busy() && session.project().assets.size() == beforeImport.assets.size() + 1
          && session.project().tracks[0].sample != beforeImport.tracks[0].sample
          && session.project().clips.size() == beforeImport.clips.size(), "file import loads sampler without placing audio clip");
    session.undoEdit(); check(session.project() == beforeImport, "sampler file import undoes in one step");
    std::cout << "MIDI / sampler / pitch / velocity / trimmed samples / persistence / undo / seek: passed\n";
}
void metronomeChecks()
{
    AudioEngine engine; engine.configureOffline(48000, 480);
    Snapshot snapshot; snapshot.generation = 1; snapshot.revision = 1;
    juce::AudioBuffer<float> buffer(2, 480);
    engine.replace(snapshot); awaitRevision(engine, 1, buffer);
    check(!engine.metronome(), "metronome defaults off");
    engine.setMetronome(true); engine.play(); render(engine, buffer);
    const auto accent = buffer.getMagnitude(0, 480);
    check(accent > 0.18f && engine.playing(), "metronome plays on an empty arrangement");
    near(buffer.getSample(0, 100), buffer.getSample(1, 100), 0, "metronome is centred");
    const auto at = [&](double seconds)
    {
        engine.stop(); engine.seek(seconds);
        for (int wait = 0; wait < 2000 && std::abs(engine.position() - seconds) > 1.0e-9; ++wait)
        { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        near(engine.position(), seconds, 1.0e-9, "metronome seek reaches selected time");
        engine.play(); render(engine, buffer); return buffer.getMagnitude(0, 480);
    };
    near(at(0.25), 0, 0, "metronome silent between beats");
    const auto beat = at(0.5);
    check(beat > 0.1f && beat < accent, "120 BPM beat and downbeat accent");
    check(at(2) > beat, "every fourth beat is accented");
    snapshot.project.bpm = 60; ++snapshot.revision; engine.submit(snapshot, false);
    near(at(0.5), 0, 0, "tempo changes immediately without rebuilding voices");
    check(at(1) > 0.1f, "60 BPM beat follows project tempo");
    engine.stop(); render(engine, buffer); near(buffer.getMagnitude(0, 480), 0, 0, "paused metronome is silent");
    engine.setMaster(0); at(1); render(engine, buffer);
    near(buffer.getMagnitude(0, 480), 0, 0, "master volume controls metronome");
    engine.setMetronome(false); engine.play(); render(engine, buffer);
    check(engine.playing() && !engine.metronome(), "disabling metronome keeps transport running");
    std::cout << "metronome / tempo / accent / seek / pause / master: passed\n";
}
void presenterChecks()
{
    struct Dialogs : EditorPresenter::Dialogs
    {
        EditorPresenter::SaveChoice choice = EditorPresenter::SaveChoice::cancel;
        juce::File directory;
        int saveRequests = 0;
        void askSaveChanges(std::function<void(EditorPresenter::SaveChoice)> reply) override { reply(choice); }
        void chooseSaveDirectory(std::function<void(juce::File)> reply) override { ++saveRequests; reply(directory); }
        void confirmOverwrite(const juce::File&, std::function<void(bool)> reply) override { reply(true); }
    };
    Workspace workspace; Storage storage; ProjectSession session; AudioEngine engine;
    EditorPresenter presenter(session, storage, engine);
    Dialogs oldView, newView; presenter.attach(oldView); presenter.newProject();
    const auto track = presenter.addTrack();
    auto source = fixture(workspace.root, 44100, 1);
    presenter.importFiles({ source.getFullPathName() }, track, 0.5);
    presenter.detach(oldView); presenter.attach(newView);
    for (int wait = 0; wait < 2000 && presenter.busy(); ++wait) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    check(!presenter.busy() && presenter.project().clips.size() == 1 && presenter.loading().empty(), "presenter import survives view replacement");
    const auto clip = presenter.project().clips[0].id;
    auto second = presenter.project().clips[0]; second.id = newId(); second.start = 2;
    presenter.insertClip(second, false);
    const auto selectionRevision = session.snapshot().revision; const auto selectionDirty = presenter.dirty();
    presenter.selectClip(clip); presenter.selectClip(second.id, true);
    check(presenter.viewState().selectedClips.size() == 2, "additive selection");
    presenter.selectClip(clip, true);
    check(presenter.viewState().selectedClips == juce::StringArray { second.id }, "toggle selection off");
    presenter.selectClips({ clip, clip, second.id, "missing" });
    check(presenter.viewState().selectedClips.size() == 2 && session.snapshot().revision == selectionRevision
          && presenter.dirty() == selectionDirty, "selection filters IDs without editing the project");
    presenter.setZoom(40000); near(presenter.viewState().pixelsPerSecond, 40000, 0, "expanded zoom limit");
    presenter.setZoom(80000); near(presenter.viewState().pixelsPerSecond, 40000, 0, "zoom clamps to expanded limit");
    presenter.setZoom(120); presenter.setSnap(false); presenter.setScroll(100, 40);
    const auto resizeRevision = session.snapshot().revision;
    presenter.setTrackHeight(track, 140); presenter.setTrackHeight("missing", 200);
    check(presenter.viewState().trackHeights.at(track) == 140 && presenter.viewState().trackHeights.size() == 1
          && session.snapshot().revision == resizeRevision, "track height changes only the view");
    presenter.attach(oldView); presenter.attach(newView); presenter.detach(oldView);
    newView.directory = workspace.root.getChildFile("presenter-save"); presenter.save(); presenter.setBpm(88);
    for (int wait = 0; wait < 2000 && presenter.busy(); ++wait) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    check(newView.saveRequests == 1 && oldView.saveRequests == 0, "old view detach preserves replacement dialog port");
    check(presenter.dirty() && session.location() == newView.directory, "presenter save preserves newer edits");
    near(Storage::load(newView.directory).project.bpm, 120, 0, "presenter saves captured revision");
    check(presenter.viewState().selectedClips == juce::StringArray { clip, second.id } && !presenter.viewState().snap && presenter.viewState().scrollX == 100
          && presenter.viewState().trackHeights.at(track) == 140,
          "presentation state survives view replacement and save");
    presenter.deleteSelection(); check(presenter.project().clips.empty(), "presenter deletes selected clips");
    presenter.undo(); check(presenter.project().clips.size() == 2, "presenter group deletion has one undo step");
    presenter.selectAllClips(); session.deleteClip(clip); session.dispatchPendingMessages();
    check(presenter.viewState().selectedClips == juce::StringArray { second.id }, "removed clips are pruned from selection");
    presenter.undo();
    near(presenter.viewState().pixelsPerSecond, 120, 0, "presenter retains zoom");
    presenter.newProject(); check(presenter.project().tracks.size() == 3, "cancel protects current project");
    newView.choice = EditorPresenter::SaveChoice::discard; presenter.newProject();
    check(presenter.project().tracks.size() == 2 && !presenter.project().tracks[0].midi && presenter.project().tracks[1].midi
          && !presenter.dirty() && presenter.viewState().selectedClips.isEmpty(), "new creates audio and MIDI tracks and resets presentation state");
    presenter.addTrack(); const auto before = session.snapshot(); presenter.open(workspace.root.getChildFile("missing"));
    for (int wait = 0; wait < 2000 && presenter.busy(); ++wait) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    check(presenter.project() == before.project && session.snapshot().generation == before.generation, "failed open preserves active project");
    presenter.detach(newView);
    std::cout << "presenter / jobs / view replacement / save revision / dialogs: passed\n";
}

void clipboardChecks()
{
    Workspace workspace; Storage storage; ProjectSession session; AudioEngine engine;
    EditorPresenter presenter(session, storage, engine);
    auto snapshot = audioProject(storage.workspace(), fixture(workspace.root, 48000, 2));
    snapshot.project.tracks = { { "one", "One" }, { "two", "Two" }, { "three", "Three" } };
    const auto asset = snapshot.project.assets[0].id;
    snapshot.project.clips = { { "first", "one", asset, 0, 0.25, 2 }, { "second", "two", asset, 0.5, 1, 2 } };
    session.replace(snapshot.project, storage.workspace(), true);
    const auto initial = session.project(); const auto revision = session.snapshot().revision;
    juce::AudioBuffer<float> buffer(2, 256);
    awaitRevision(engine, revision, buffer);
    presenter.selectTime({ 0.75, 1.25, { "two" } }); presenter.togglePlayback();
    for (int wait = 0; wait < 2000 && !engine.playing(); ++wait)
    { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    check(engine.playing(), "Play starts after selection seek is prepared");
    near(engine.position(), 0.75 + 256.0 / 48000, 1.0e-9, "Play starts at the selected time");
    presenter.togglePlayback(); check(!engine.playing(), "Play/Pause pauses selection playback");
    presenter.togglePlayback();
    for (int wait = 0; wait < 2000 && !engine.playing(); ++wait)
    { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    near(engine.position(), 0.75 + 256.0 / 48000, 1.0e-9, "Play after Pause restarts at the selected time");
    presenter.stop();
    presenter.selectTime({ 1.25, 1.25, { "one" } }); presenter.togglePlayback();
    for (int wait = 0; wait < 2000 && !engine.playing(); ++wait)
    { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    near(engine.position(), 1.25 + 256.0 / 48000, 1.0e-9, "Play starts at a clicked insertion marker");
    presenter.stop(); presenter.selectClip("second"); presenter.togglePlayback();
    for (int wait = 0; wait < 2000 && !engine.playing(); ++wait)
    { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    near(engine.position(), 0.5 + 256.0 / 48000, 1.0e-9, "Play starts at a selected clip");
    presenter.stop(); presenter.selectClips({}); presenter.togglePlayback();
    for (int wait = 0; wait < 2000 && !engine.playing(); ++wait)
    { render(engine, buffer); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    near(engine.position(), 256.0 / 48000, 1.0e-9, "without selection Play restarts at zero");
    presenter.stop();
    presenter.selectTime({ 0.5, 1, { "one", "two" } }); presenter.copySelection();
    check(session.snapshot().revision == revision && !session.dirty(), "copying time does not edit the project");
    presenter.duplicateSelection();
    check(session.project().clips.size() == 6, "duplicate overwrites covered audio and preserves remainders");
    near(startingClip(session.project(), "one", 1).offset, 0.75, 0, "duplicate preserves first source offset");
    near(startingClip(session.project(), "two", 1).offset, 1, 0, "duplicate preserves second source offset");
    near(startingClip(session.project(), "one", 1).duration, 0.5, 0, "duplicate copies only the selected duration");
    session.undoEdit(); check(session.project() == initial, "duplicate is one undo step");
    presenter.selectAllClips(); presenter.duplicateSelection();
    near(session.project().clips[2].start, 2.5, 0, "whole-clip duplicate follows the complete selection");
    near(session.project().clips[3].start, 3, 0, "whole-clip duplicate retains spacing");
    session.undoEdit(); check(session.project() == initial, "whole-clip duplicate undo");
    presenter.selectTime({ 2, 2, { "three" } }); presenter.pasteSelection();
    check(session.project().clips.size() == 4 && session.project().tracks.size() == 4,
          "paste adds tracks when the copied selection needs them");
    near(session.project().clips[2].start, 2, 0, "paste targets the insertion marker");
    near(session.project().clips[2].offset, 0.75, 0, "duplicate does not overwrite the copied selection");
    check(session.project().clips[2].track == "three" && session.project().clips[3].track == session.project().tracks[3].id,
          "paste retains relative track positions");
    const auto pasted = session.project();
    session.undoEdit(); check(session.project() == initial, "paste and added tracks undo together");
    session.redoEdit(); check(session.project() == pasted, "paste redo keeps clip IDs and audio offsets");
    session.undoEdit();
    presenter.selectTime({ 0.5, 1, { "one", "two" } }); presenter.deleteSelection();
    check(session.project().clips.size() == 3 && session.project().assets == initial.assets,
          "Delete removes selected time and preserves the surrounding audio");
    session.undoEdit(); check(session.project() == initial, "partial deletion is one undo step");
    presenter.selectClip("first"); presenter.copySelection(); presenter.selectTime({ 3, 3, { "two" } }); presenter.pasteSelection();
    near(session.project().clips.back().offset, 0.25, 0, "whole-clip copy retains source offset");
    near(session.project().clips.back().duration, 2, 0, "whole-clip paste retains duration");
    check(session.project().clips.back().id != "first" && session.project().clips.back().track == "two"
          && presenter.viewState().selectedClips.size() == 1, "paste creates and selects a new clip");
    session.saved(session.snapshot(), session.location()); presenter.newProject(); presenter.pasteSelection();
    check(session.project().clips.empty() && session.project().tracks.size() == 2, "new projects clear the audio clipboard");
    std::cout << "time selection / duplicate / copy / paste / partial deletion: passed\n";
}

void workflowChecks()
{
    Workspace workspace; Storage storage; ProjectSession session; AudioEngine engine;
    juce::AudioBuffer<float> buffer(2, 256); engine.configureOffline(48000, 256);
    uint64_t generation = 0;
    session.onEdit = [&](const Snapshot& snapshot, bool structural)
    { if (snapshot.generation != generation) { generation = snapshot.generation; engine.replace(snapshot); }
      else engine.submit(snapshot, structural); };
    session.replace({}, storage.workspace(), true);
    const auto first = session.addTrack("Stereo"), second = session.addTrack("Mono");
    auto stereo = Storage::copyAudio(fixture(workspace.root, 48000, 2, 8, true), storage.workspace());
    auto mono = Storage::copyAudio(fixture(workspace.root, 44100, 1, 8, true), storage.workspace());
    check(stereo.result.wasOk() && mono.result.wasOk(), "workflow imports mixed rates");
    session.importAsset(stereo.asset, Clip { newId(), first, stereo.asset.id, 0, 0, 6 });
    session.importAsset(mono.asset, Clip { newId(), second, mono.asset.id, 0, 0, 6 });
    awaitRevision(engine, session.snapshot().revision, buffer); engine.play(); render(engine, buffer);
    const auto startingCursor = engine.position();
    auto clip = session.project().clips[0]; clip.start = 0.03; session.changeClip(clip);
    awaitRevision(engine, session.snapshot().revision, buffer);
    clip = trimLeft(clip, 0.06); clip = trimRight(clip, 5.5, 8); session.changeClip(clip);
    awaitRevision(engine, session.snapshot().revision, buffer);
    session.deleteClip(clip.id); awaitRevision(engine, session.snapshot().revision, buffer);
    session.undoEdit(); awaitRevision(engine, session.snapshot().revision, buffer);
    session.redoEdit(); awaitRevision(engine, session.snapshot().revision, buffer);
    session.undoEdit(); awaitRevision(engine, session.snapshot().revision, buffer);
    session.deleteTrack(first); awaitRevision(engine, session.snapshot().revision, buffer);
    session.undoEdit(); awaitRevision(engine, session.snapshot().revision, buffer);
    session.controls(first, 0.5, true); render(engine, buffer); session.controls(first, 0.75, false);
    check(engine.playing() && engine.position() > startingCursor, "workflow edits / undo do not stop transport");
    const auto source = fixture(workspace.root, 32000, 1, 8, true);
    const auto importedAt = engine.position(); bool imported = false;
    storage.importFile(source, storage.workspace(), [&](ImportResult result)
    {
        check(result.result.wasOk(), "workflow background import");
        session.importAsset(result.asset, Clip { newId(), second, result.asset.id, importedAt, 0, 5 }); imported = true;
    });
    for (int wait = 0; wait < 2000 && !imported; ++wait)
    { render(engine, buffer); juce::MessageManager::getInstance()->runDispatchLoopUntil(2); }
    check(imported && engine.playing(), "workflow imports while playing");
    awaitRevision(engine, session.snapshot().revision, buffer);
    const auto captured = session.snapshot(); const auto destination = workspace.root.getChildFile("saved");
    bool saved = false;
    storage.save(captured, destination, [&](juce::Result result)
    { check(result.wasOk(), "workflow background save"); session.saved(captured, destination); saved = true; });
    session.setBpm(87);
    for (int wait = 0; wait < 2000 && !saved; ++wait)
    { render(engine, buffer); juce::MessageManager::getInstance()->runDispatchLoopUntil(2); }
    check(saved && session.dirty() && engine.playing(), "workflow saving keeps playback and newer dirty edit");
    const auto copy = workspace.root.getChildFile("save-as"); const auto saveAsSnapshot = session.snapshot(); saved = false;
    storage.save(saveAsSnapshot, copy, [&](juce::Result result)
    { check(result.wasOk(), "workflow Save As"); session.saved(saveAsSnapshot, copy); saved = true; });
    const auto savingCursor = engine.position();
    for (int wait = 0; wait < 2000 && !saved; ++wait)
    { render(engine, buffer); juce::MessageManager::getInstance()->runDispatchLoopUntil(2); }
    check(saved && !session.dirty() && engine.playing() && engine.position() > savingCursor, "workflow Save As does not stop");
    const auto expected = session.project(); engine.stop(); storage.cancel();
    const auto moved = workspace.root.getChildFile("relocated"); check(copy.moveFileTo(moved), "workflow relocate");
    auto loaded = Storage::load(moved);
    if (!(loaded.project == expected))
    {
        std::cerr << "load: " << loaded.result.getErrorMessage() << " tracks=" << (loaded.project.tracks == expected.tracks)
                  << " assets=" << (loaded.project.assets == expected.assets) << " clips=" << (loaded.project.clips == expected.clips) << '\n';
        for (size_t i = 0; i < loaded.project.clips.size() && i < expected.clips.size(); ++i)
            std::cerr << std::setprecision(17) << "start " << loaded.project.clips[i].start << "/" << expected.clips[i].start
                      << " offset " << loaded.project.clips[i].offset << "/" << expected.clips[i].offset
                      << " duration " << loaded.project.clips[i].duration << "/" << expected.clips[i].duration << '\n';
    }
    check(loaded.result.wasOk() && loaded.project == expected, "workflow portable exact roundtrip");
    session.replace(loaded.project, moved, true); awaitRevision(engine, session.snapshot().revision, buffer);
    moved.getChildFile(loaded.project.assets[0].path).deleteFile(); loaded = Storage::load(moved);
    check(loaded.result.wasOk() && loaded.project.assets[0].missing && loaded.project.clips.size() == expected.clips.size(), "workflow missing clip preserved");
    session.replace(loaded.project, moved, true); awaitRevision(engine, session.snapshot().revision, buffer);
    engine.play(); render(engine, buffer); render(engine, buffer);
    check(buffer.getMagnitude(0, 256) > 0 && engine.playing(), "workflow other tracks play with missing media");
    check(callbackAllocations.load() == 0 && callbackFrees.load() == 0, "workflow callback ownership audit");
    session.onEdit = nullptr;
    std::cout << "end-to-end backend acceptance scenario: passed\n";
}

void performance(int seconds, bool hardware)
{
    Workspace workspace;
    auto snapshot = audioProject(workspace.root.getChildFile("project"), fixture(workspace.root, 48000, 2, seconds + 30, true));
    snapshot.project.tracks.clear();
    for (int i = 0; i < 16; ++i)
    {
        const auto id = juce::String(i); snapshot.project.tracks.push_back({ id, "Track " + id, 0.05, false });
        snapshot.project.clips.push_back({ "clip" + id, id, snapshot.project.assets[0].id, 0, 0, static_cast<double>(seconds + 20) });
    }
    AudioEngine engine; juce::AudioBuffer<float> buffer(2, 256);
    if (hardware)
    {
        check(engine.initialise().isEmpty(), "hardware device initialization");
        auto settings = engine.devices.getAudioDeviceSetup(); settings.sampleRate = 48000; settings.bufferSize = 256;
        check(engine.devices.setAudioDeviceSetup(settings, true).isEmpty(), "hardware 48k / 256 setup");
        if (auto* device = engine.devices.getCurrentAudioDevice())
            std::cout << "Starting hardware " << device->getName() << " open=" << device->isOpen() << " playing=" << device->isPlaying() << '\n' << std::flush;
    }
    else engine.configureOffline(48000, 256);
    engine.replace(snapshot);
    if (!hardware) awaitRevision(engine, 1, buffer);
    else
    { for (int i = 0; i < 10000 && engine.appliedRevision() != 1; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
      check(engine.appliedRevision() == 1, "hardware arrangement ready"); }
    engine.play();
    std::cout << "performance mode=" << (hardware ? "hardware" : "offline-paced") << " rate=" << engine.rate();
    if (auto* device = engine.devices.getCurrentAudioDevice())
        std::cout << " device=" << device->getName() << " backend=" << device->getTypeName() << " buffer=" << device->getCurrentBufferSizeSamples();
    std::cout << " seconds=" << seconds << " tracks=16\n" << std::flush;
    const auto start = std::chrono::steady_clock::now(); auto next = start, nextEdit = start;
    double latencySum = 0, latencyMax = 0; int count = 0; std::optional<std::chrono::steady_clock::time_point> submitted;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds))
    {
        const auto now = std::chrono::steady_clock::now();
        if (submitted && engine.appliedRevision() == snapshot.revision)
        { const auto latency = std::chrono::duration<double, std::milli>(now - *submitted).count(); latencySum += latency; latencyMax = std::max(latencyMax, latency); ++count; submitted.reset(); }
        if (now >= nextEdit)
        { snapshot.project.clips[0].start = snapshot.project.clips[0].start > 0 ? 0 : 0.02; ++snapshot.revision;
          engine.submit(snapshot); submitted = now; nextEdit = now + std::chrono::milliseconds(500); }
        if (!hardware) render(engine, buffer);
        else juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        next += std::chrono::microseconds(5333); std::this_thread::sleep_until(next);
    }
    std::cout << "underruns=" << engine.underrunCount() << " cursor=" << engine.position()
              << " activation_mean_ms=" << (count ? latencySum / count : 0) << " activation_max_ms=" << latencyMax
              << " edits=" << count << " callback_allocations=" << callbackAllocations.load() << " callback_frees=" << callbackFrees.load() << '\n';
    check(engine.underrunCount() == 0, "performance zero underruns target");
}
}
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    try
    {
        if (argc > 1 && juce::String(argv[1]) == "--performance")
            performance(argc > 2 ? std::atoi(argv[2]) : 600, argc > 3 && juce::String(argv[3]) == "--hardware");
        else { modelChecks(); overlapChecks(); storageChecks(); audioChecks(); midiChecks(); metronomeChecks(); presenterChecks(); clipboardChecks(); workflowChecks(); }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
