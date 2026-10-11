cmake_minimum_required(VERSION 3.22)
project(Opentone VERSION 0.0.1 LANGUAGES C CXX)
add_compile_definitions(JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}> JUCE_PLUGINHOST_VST=0)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/91ad83ae34a81e0833b1a2b0866f54846370ae53.tar.gz
    URL_HASH SHA256=04f8d5055382582c757be9da069ea98338005f98248facd9c2804435ac853e70)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(Opentone
    PRODUCT_NAME "Opentone"
    COMPANY_NAME "Opentone"
    BUNDLE_ID "org.opentone.desktop"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Opentone records microphone audio into armed audio tracks."
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/opentone.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/opentone.png")
target_include_directories(Opentone PRIVATE src)
option(OPENTONE_LIVE_UI "Build the development host and reloadable C++ UI" OFF)
set(opentone_juce_modules
    juce_gui_basics juce_audio_devices juce_audio_formats juce_audio_basics
    juce_data_structures juce_audio_utils juce_gui_extra juce_graphics juce_events
    juce_core juce_audio_processors juce_audio_processors_headless)
if(NOT APPLE)
    add_compile_definitions(JUCE_USE_MP3AUDIOFORMAT=1)
endif()
if(OPENTONE_LIVE_UI)
    # One JUCE runtime ensures host and UI share devices, message manager and allocators.
    add_library(OpentoneRuntime SHARED src/infrastructure/Storage.cpp src/infrastructure/AudioEngine.cpp src/infrastructure/PluginHost.cpp src/domain/ProjectSession.cpp src/application/EditorPresenter.cpp)
    target_include_directories(OpentoneRuntime PUBLIC "${juce_SOURCE_DIR}/modules" src)
    target_compile_features(OpentoneRuntime PUBLIC cxx_std_17)
    target_compile_definitions(OpentoneRuntime PUBLIC
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_STANDALONE_APPLICATION=1
        $<$<CONFIG:Debug>:DEBUG=1> $<$<CONFIG:Debug>:_DEBUG=1> $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 OPENTONE_SHARED_RUNTIME=1)
    target_compile_definitions(OpentoneRuntime PRIVATE JUCE_DLL_BUILD=1)
    target_compile_definitions(OpentoneRuntime INTERFACE JUCE_DLL=1)
    foreach(module IN LISTS opentone_juce_modules)
        target_link_libraries(OpentoneRuntime PRIVATE juce::${module})
        target_compile_definitions(OpentoneRuntime INTERFACE JUCE_MODULE_AVAILABLE_${module}=1)
    endforeach()
    target_link_libraries(OpentoneRuntime PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(WIN32)
        set(opentone_live_dir "${CMAKE_BINARY_DIR}/Opentone_artefacts/${CMAKE_BUILD_TYPE}")
    else()
        set(opentone_live_dir "${CMAKE_BINARY_DIR}/live")
    endif()
    set_target_properties(OpentoneRuntime PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${opentone_live_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${opentone_live_dir}" WINDOWS_EXPORT_ALL_SYMBOLS ON)
    add_library(OpentoneUI MODULE src/ui/Editor.cpp src/ui/Browser.cpp src/ui/Transport.cpp src/ui/Timeline.cpp src/ui/Mixer.cpp src/ui/MidiEditor.cpp src/ui/UiTheme.cpp src/ui/UiModule.cpp)
    target_link_libraries(OpentoneUI PRIVATE OpentoneRuntime)
    target_compile_definitions(OpentoneUI PRIVATE OPENTONE_UI_MODULE_BUILD=1)
    set_target_properties(OpentoneUI PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${opentone_live_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${opentone_live_dir}")
    target_sources(Opentone PRIVATE src/app/Main.cpp src/app/UiLoader.cpp)
    target_link_libraries(Opentone PRIVATE OpentoneRuntime ${CMAKE_DL_LIBS})
    target_compile_definitions(Opentone PRIVATE OPENTONE_LIVE_UI=1 OPENTONE_UI_MODULE_PATH="$<TARGET_FILE:OpentoneUI>")
    add_dependencies(Opentone OpentoneUI)
else()
    target_sources(Opentone PRIVATE src/app/Main.cpp src/infrastructure/Storage.cpp src/infrastructure/AudioEngine.cpp src/infrastructure/PluginHost.cpp src/domain/ProjectSession.cpp src/application/EditorPresenter.cpp src/ui/Editor.cpp src/ui/Browser.cpp src/ui/Transport.cpp src/ui/Timeline.cpp src/ui/Mixer.cpp src/ui/MidiEditor.cpp src/ui/UiTheme.cpp src/ui/UiModule.cpp)
    target_compile_features(Opentone PRIVATE cxx_std_17)
    target_compile_definitions(Opentone PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    foreach(module IN LISTS opentone_juce_modules)
        target_link_libraries(Opentone PRIVATE juce::${module})
    endforeach()
    target_link_libraries(Opentone PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
set_target_properties(Opentone PROPERTIES CXX_EXTENSIONS OFF)
if(APPLE)
    set(opentone_license_dir "$<TARGET_BUNDLE_DIR:Opentone>/Contents/Resources")
else()
    set(opentone_license_dir "$<TARGET_FILE_DIR:Opentone>")
endif()
add_custom_command(TARGET Opentone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "${opentone_license_dir}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${opentone_license_dir}")
if(APPLE)
    add_custom_command(TARGET Opentone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:Opentone>/licenses"
            "$<TARGET_FILE_DIR:Opentone>/LICENSE" "$<TARGET_FILE_DIR:Opentone>/THIRD_PARTY_NOTICES.md")
    if(OPENTONE_LIVE_UI)
        target_sources(OpentoneUI PRIVATE src/ui/NativeWindow.mm)
    else()
        target_sources(Opentone PRIVATE src/ui/NativeWindow.mm)
    endif()
    target_sources(Opentone PRIVATE LICENSE THIRD_PARTY_NOTICES.md)
    set_source_files_properties(LICENSE THIRD_PARTY_NOTICES.md PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

include(CTest)
if(BUILD_TESTING)
    if(OPENTONE_LIVE_UI)
        target_compile_definitions(OpentoneUI PRIVATE OPENTONE_UI_TESTING=1)
        target_sources(Opentone PRIVATE tests/ui/SelectionChecks.cpp)
        target_include_directories(Opentone PRIVATE tests/ui)
        target_compile_definitions(Opentone PRIVATE OPENTONE_SELECTION_CHECKS=1)
        add_library(OpentoneBadUI MODULE tests/ui/BadUi.cpp)
        target_link_libraries(OpentoneBadUI PRIVATE OpentoneRuntime)
        target_compile_definitions(OpentoneBadUI PRIVATE OPENTONE_UI_MODULE_BUILD=1)
        set_target_properties(OpentoneBadUI PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${opentone_live_dir}")
        target_compile_definitions(Opentone PRIVATE OPENTONE_BAD_UI_MODULE_PATH="$<TARGET_FILE:OpentoneBadUI>")
        add_dependencies(Opentone OpentoneBadUI)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_program(OPENTONE_XVFB xvfb-run)
            if(OPENTONE_XVFB)
                add_test(NAME live-ui COMMAND ${OPENTONE_XVFB} -a $<TARGET_FILE:Opentone> --live-ui-smoke-test)
            endif()
        else()
            add_test(NAME live-ui COMMAND Opentone --live-ui-smoke-test)
        endif()
    endif()
    juce_add_console_app(OpentoneChecks PRODUCT_NAME "Opentone checks")
    target_sources(OpentoneChecks PRIVATE tests/core/Checks.cpp src/infrastructure/Storage.cpp src/infrastructure/AudioEngine.cpp src/infrastructure/PluginHost.cpp src/domain/ProjectSession.cpp src/application/EditorPresenter.cpp)
    target_include_directories(OpentoneChecks PRIVATE src)
    target_compile_features(OpentoneChecks PRIVATE cxx_std_17)
    target_compile_definitions(OpentoneChecks PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 OPENTONE_TESTING=1)
    target_link_libraries(OpentoneChecks PRIVATE juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_formats
        juce::juce_data_structures juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME focused COMMAND OpentoneChecks)
    add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure DEPENDS OpentoneChecks)
    if(OPENTONE_LIVE_UI)
        add_dependencies(check Opentone)
    endif()
endif()

if(APPLE)
    if(OPENTONE_LIVE_UI)
        set(opentone_recording_targets OpentoneRuntime)
    else()
        set(opentone_recording_targets Opentone)
    endif()
    if(TARGET OpentoneChecks)
        list(APPEND opentone_recording_targets OpentoneChecks)
    endif()
    find_library(opentone_avfoundation AVFoundation REQUIRED)
    foreach(recording_target IN LISTS opentone_recording_targets)
        target_sources(${recording_target} PRIVATE src/infrastructure/Microphone.mm)
        target_link_libraries(${recording_target} PRIVATE ${opentone_avfoundation})
    endforeach()
    # Local builds need a stable signing identity for macOS microphone permission.
    find_program(opentone_codesign codesign REQUIRED)
    add_custom_command(TARGET Opentone POST_BUILD
        COMMAND ${opentone_codesign} --force --sign - --identifier org.opentone.desktop
            --requirements "=designated => identifier \"org.opentone.desktop\""
            "$<TARGET_BUNDLE_DIR:Opentone>"
        VERBATIM)
endif()
