// SPDX-License-Identifier: AGPL-3.0-only
#include "app/UiModule.h"
#if defined(OPENTONE_LIVE_UI)
#include "app/UiLoader.h"
#if defined(OPENTONE_SELECTION_CHECKS)
#include "SelectionChecks.h"
#endif
#endif

namespace opentone
{
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const UiApi& api, EditorPresenter& presenter)
        : DocumentWindow(presenter.windowTitle(), juce::Colour(0xff202227), allButtons)
    {
#if JUCE_MAC
        setUsingNativeTitleBar(false); setTitleBarHeight(28); setTitleBarTextCentred(true);
        setTitleBarButtonsRequired(allButtons, true);
#else
        setUsingNativeTitleBar(true);
#endif
        setContentOwned(api.create(presenter), true);
        setResizable(true, false); setResizeLimits(900, 540, 4000, 2400);
        centreWithSize(getWidth(), getHeight()); setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
#if JUCE_MAC
    void activeWindowStatusChanged() override
    {
        juce::DocumentWindow::activeWindowStatusChanged();
        // AeroSpace checks fullscreen availability even before a window gains focus.
        for (auto* button : { getCloseButton(), getMinimiseButton(), getMaximiseButton() })
            if (button) button->setEnabled(true);
    }
#endif
};
class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Opentone"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    void initialise(const juce::String& commandLine) override
    {
        if (PluginHost::scanChild(commandLine)) return;
        juce::LookAndFeel::setDefaultLookAndFeel(&look);
        session = std::make_unique<ProjectSession>(); storage = std::make_unique<Storage>(); engine = std::make_unique<AudioEngine>();
        preferences = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("Opentone/device.xml");
#if defined(OPENTONE_LIVE_UI)
        preferences = preferences.getSiblingFile("device-dev.xml");
#endif
        smoke = commandLine.trim() == "--smoke-test";
#if defined(OPENTONE_LIVE_UI)
        reloadSmoke = commandLine.trim() == "--live-ui-smoke-test";
        smoke = smoke || reloadSmoke;
#endif
        if (!smoke)
        {
            auto settings = juce::XmlDocument::parse(preferences);
            engine->initialise(settings.get());
        }
        presenter = std::make_unique<EditorPresenter>(*session, *storage, *engine);
        presenter->newProject();
#if defined(OPENTONE_LIVE_UI)
        loader = std::make_unique<UiLoader>(juce::File(OPENTONE_UI_MODULE_PATH));
        ui = loader->load();
        if (!ui) { setApplicationReturnValue(1); quit(); return; }
#else
        ui = opentoneGetUiApi();
#endif
        window = std::make_unique<MainWindow>(*ui, *presenter);
#if defined(OPENTONE_LIVE_UI)
        if (reloadSmoke && !prepareReloadSmoke()) { setApplicationReturnValue(1); quit(); return; }
#endif
        if (smoke) startTimer(500);
#if defined(OPENTONE_LIVE_UI)
        else startTimer(250);
#endif
    }
    void systemRequestedQuit() override
    {
        if (smoke || !window) { quit(); return; }
        ui->requestQuit(*window->getContentComponent(), [] { quit(); });
    }
    void shutdown() override
    {
        stopTimer();
        if (engine && !smoke)
            if (auto settings = engine->devices.createStateXml())
            { preferences.getParentDirectory().createDirectory(); settings->writeTo(preferences); }
        window.reset();
        presenter.reset();
        engine.reset(); storage.reset(); session.reset();
#if defined(OPENTONE_LIVE_UI)
        loader.reset();
#endif
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
private:
    void timerCallback() override
    {
#if defined(OPENTONE_LIVE_UI)
        if (!smoke)
        {
            if (window && loader->changed() && ui->canReload(*window->getContentComponent()))
                if (const auto* next = loader->load())
                {
                    replaceEditor(next);
                }
            return;
        }
#endif
#if defined(OPENTONE_LIVE_UI)
        if (reloadSmoke)
        {
            juce::AudioBuffer<float> buffer(2, 256);
            engine->render(buffer.getArrayOfWritePointers(), 2, 256);
            if (engine->appliedRevision() != session->snapshot().revision && ++reloadSmokeTries < 20) return;
            if (!checkReloadSmoke()) { setApplicationReturnValue(1); quit(); return; }
        }
#endif
        stopTimer();
        if (!window || !window->isVisible() || !window->getContentComponent()) setApplicationReturnValue(1);
        else
        {
            auto& editor = *window->getContentComponent();
            const auto image = editor.createComponentSnapshot(editor.getLocalBounds());
            auto output = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                              .getSiblingFile("window-smoke.png").createOutputStream();
            juce::PNGImageFormat png;
            if (output) { output->setPosition(0); output->truncate(); }
            if (!output || !png.writeImageToStream(image, *output)) setApplicationReturnValue(1);
        }
        quit();
    }
#if defined(OPENTONE_LIVE_UI)
    bool replaceEditor(const UiApi* next)
    {
        std::unique_ptr<juce::Component> editor(next->create(*presenter));
        if (!editor) { juce::Logger::writeToLog("UI factory returned no component; keeping current editor."); return false; }
        window->setContentOwned(editor.release(), false); ui = next;
        window->getContentComponent()->grabKeyboardFocus();
        juce::Logger::writeToLog("UI reloaded; project and playback retained.");
        return true;
    }
    bool prepareReloadSmoke()
    {
        reloadInitialProject = session->project();
        presenter->addTrack();
        const auto file = storage->workspace().getChildFile("Audio/live-check.wav");
        if (file.getParentDirectory().createDirectory().failed()) return false;
        std::unique_ptr<juce::OutputStream> output = file.createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(output, juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        if (!writer) return false;
        juce::AudioBuffer<float> data(2, 256);
        for (int channel = 0; channel < 2; ++channel) juce::FloatVectorOperations::fill(data.getWritePointer(channel), 0.1f, 256);
        for (int i = 0; i < 375; ++i) if (!writer->writeFromAudioSampleBuffer(data, 0, 256)) return false;
        writer.reset();
        const auto track = session->project().tracks[0].id; const auto asset = newId();
        return session->importAsset({ asset, "Audio/live-check.wav", 48000, 2, 96000, false },
                                    Clip { newId(), track, asset, 0, 0, 2 })
            && session->insertClip({ newId(), track, asset, 2.5, 0, 2 });
    }
    bool checkReloadSmoke()
    {
        juce::AudioBuffer<float> buffer(2, 256);
        engine->render(buffer.getArrayOfWritePointers(), 2, 256);
        juce::Logger::writeToLog("Reload smoke: applied " + juce::String(engine->appliedRevision()) + " requested " + juce::String(session->snapshot().revision));
        if (engine->appliedRevision() != session->snapshot().revision) return false;
        session->beginGesture();
        const bool gestureBlocks = !ui->canReload(*window->getContentComponent()); session->endGesture(true);
        if (!gestureBlocks) { juce::Logger::writeToLog("Gesture gate failed"); return false; }
        window->getContentComponent()->grabKeyboardFocus();
        if (!ui->canReload(*window->getContentComponent())) { juce::Logger::writeToLog("Idle gate failed"); return false; }
        engine->play(); engine->render(buffer.getArrayOfWritePointers(), 2, 256);
        engine->setMaster(0.5f);
        presenter->selectAllClips();
        presenter->setZoom(120); presenter->setSnap(false); presenter->setScroll(80, 0);
        presenter->setTrackHeight(session->project().tracks[0].id, 120);
        const auto viewBefore = presenter->viewState();
        const auto before = session->snapshot(); const auto cursor = engine->position();
        const auto* next = loader->load();
        if (!next || next == ui || !replaceEditor(next)) return false;
        engine->render(buffer.getArrayOfWritePointers(), 2, 256);
        if (presenter->viewState().selectedClips != viewBefore.selectedClips || presenter->viewState().snap != viewBefore.snap
            || presenter->viewState().trackHeights != viewBefore.trackHeights
            || presenter->viewState().scrollX != viewBefore.scrollX
            || std::abs(presenter->viewState().pixelsPerSecond - viewBefore.pixelsPerSecond) > 1.0e-9) return false;
        if (!(session->project() == before.project) || session->snapshot().revision != before.revision
            || !session->dirty() || !engine->playing() || engine->position() <= cursor || buffer.getMagnitude(0, 256) <= 0
            || std::abs(engine->master() - 0.5f) > 1.0e-6f) return false;
        session->undoEdit(); session->undoEdit(); session->undoEdit();
        if (!(session->project() == reloadInitialProject)) return false;
        session->redoEdit(); session->redoEdit(); session->redoEdit();
        if (!(session->project() == before.project)) return false;
#if defined(OPENTONE_SELECTION_CHECKS)
        presenter->selectTime({ 0.5, 1.0, { session->project().tracks[0].id } });
        presenter->copySelection();
        const auto timeBefore = presenter->viewState().timeSelection;
        next = loader->load();
        if (!next || !replaceEditor(next) || !(presenter->viewState().timeSelection == timeBefore)) return false;
        const auto clipboardBefore = session->project();
        presenter->selectTime({ 5, 5, { session->project().tracks[0].id } }); presenter->pasteSelection();
        if (session->project().clips.size() != clipboardBefore.clips.size() + 1
            || std::abs(session->project().clips.back().offset - 0.5) > 1.0e-9) return false;
        presenter->undo(); if (!(session->project() == clipboardBefore)) return false;
#endif
#if defined(OPENTONE_SELECTION_CHECKS)
        stopTimer();
        if (!selectionChecks(*window->getContentComponent(), *presenter, *session)) return false;
#endif
#if defined(OPENTONE_BAD_UI_MODULE_PATH)
        UiLoader invalid(juce::File(OPENTONE_BAD_UI_MODULE_PATH));
        if (invalid.load() != nullptr || invalid.error().isEmpty()) return false;
#endif
        juce::Logger::writeToLog("Live UI checks passed: playback, project, view state, runtime undo/redo, gesture gate and ABI rejection.");
        return true;
    }
    Project reloadInitialProject;
    bool reloadSmoke = false;
    int reloadSmokeTries = 0;
#endif
    const UiApi* ui = nullptr;
#if defined(OPENTONE_LIVE_UI)
    std::unique_ptr<UiLoader> loader;
#endif
    juce::LookAndFeel_V4 look;
    juce::File preferences;
    std::unique_ptr<ProjectSession> session;
    std::unique_ptr<Storage> storage;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<EditorPresenter> presenter;
    bool smoke = false;
};
}
START_JUCE_APPLICATION(opentone::Application)
