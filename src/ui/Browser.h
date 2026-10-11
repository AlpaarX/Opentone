// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "application/EditorPresenter.h"
#include <juce_audio_utils/juce_audio_utils.h>
namespace opentone
{
class Browser final : public juce::Component, public juce::ListBoxModel,
                      public juce::DragAndDropContainer, private juce::FileBrowserListener
{
public:
    explicit Browser(EditorPresenter&);
    ~Browser() override;
    int getNumRows() override;
    juce::String getNameForRow(int row) override { return row >= 0 && row < static_cast<int>(rows.size()) ? rows[static_cast<size_t>(row)].name : juce::String(); }
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>&) override;
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;
    void listBoxItemClicked(int, const juce::MouseEvent&) override;
    void returnKeyPressed(int row) override { activate(row); }
    void resized() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void refresh();
    bool canReload() const { return !choosing; }
private:
    enum class Category { sounds, instruments, effects };
    struct Row { juce::String name, drag, format; bool plugin = false; };
    class Folder;
    void selectCategory(Category);
    void chooseFolder();
    void updateSource();
    void activate(int);
    void useFile(const juce::File&);
    void selectionChanged() override;
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File&) override;
    void browserRootChanged(const juce::File&) override {}
    EditorPresenter& presenter;
    juce::Label title, empty;
    juce::TreeView categories;
    std::unique_ptr<Folder> root;
    juce::ComboBox locations;
    juce::TextEditor search;
    juce::TextButton addFolder {"+ Folder"}, removeFolder {"-"};
    juce::ListBox list {"Library items", this};
    juce::WildcardFileFilter filter {"*.wav;*.aif;*.aiff;*.flac;*.mp3", "*", "Audio files"};
    juce::TimeSliceThread directoryThread {"Sample folders"};
    juce::DirectoryContentsList directory {&filter, directoryThread};
    juce::FileTreeComponent files {directory};
    juce::StringArray cachedFolders;
    std::vector<Row> rows;
    Category category = Category::sounds;
    std::unique_ptr<juce::FileChooser> chooser;
    bool choosing = false, folderAvailable = false;
};
}
