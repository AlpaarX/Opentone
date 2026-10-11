// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/Browser.h"
#include "ui/UiTheme.h"
namespace opentone
{
class Browser::Folder final : public juce::TreeViewItem
{
public:
    Folder(Browser& browser, juce::String text, Category kind) : owner(browser), name(std::move(text)), category(kind) {}
    bool mightContainSubItems() override { return name.isEmpty(); }
    int getItemHeight() const override { return 25; }
    juce::String getUniqueName() const override { return name; }
    void paintItem(juce::Graphics& graphics, int width, int height) override
    {
        graphics.setColour(isSelected() ? accent.withAlpha(0.3f) : panel); graphics.fillRect(0, 0, width, height);
        graphics.setColour(ink); graphics.fillRect(4, 7, 6, 3); graphics.drawRect(4, 9, 14, 10);
        graphics.setFont(juce::FontOptions(12)); graphics.drawText(name, 25, 0, width - 29, height, juce::Justification::centredLeft, true);
    }
    void itemSelectionChanged(bool selected) override { if (selected && name.isNotEmpty()) owner.selectCategory(category); }
private: Browser& owner; juce::String name; Category category;
};
Browser::Browser(EditorPresenter& owner) : presenter(owner), root(std::make_unique<Folder>(*this, juce::String(), Category::sounds))
{
    for (auto* child : std::initializer_list<juce::Component*> { &title, &empty, &categories, &locations, &search, &addFolder, &removeFolder, &list, &files }) addAndMakeVisible(child);
    label(title, "LIBRARY"); list.setRowHeight(25); files.setItemHeight(25); files.setMultiSelectEnabled(false); files.addListener(this);
    categories.setRootItem(root.get()); categories.setRootItemVisible(false); categories.setMultiSelectEnabled(false);
    root->addSubItem(new Folder(*this, "Sounds", Category::sounds)); root->addSubItem(new Folder(*this, "Instruments", Category::instruments));
    root->addSubItem(new Folder(*this, "Effects", Category::effects)); root->setOpen(true);
    locations.onChange = [this] { updateSource(); refresh(); };
    addFolder.onClick = [this] { chooseFolder(); };
    removeFolder.setTitle("Remove sample folder link; keep files on disk");
    removeFolder.onClick = [this]
    { const auto index = locations.getSelectedId() - 2;
      if (index >= 0 && index < cachedFolders.size()) { const juce::File folder(cachedFolders[index]); presenter.removeSampleFolder(folder); refresh(); } };
    search.setTextToShowWhenEmpty("Search...", ink.withAlpha(0.6f)); search.onTextChange = [this] { refresh(); };
    directoryThread.startThread(); refresh(); root->getSubItem(0)->setSelected(true, true);
}
Browser::~Browser()
{ files.removeListener(this); categories.setRootItem(nullptr); directoryThread.stopThread(-1); }
void Browser::selectCategory(Category next)
{ category = next; search.setText({}, false); refresh(); }
void Browser::chooseFolder()
{
    if (choosing) return; choosing = true;
    chooser = std::make_unique<juce::FileChooser>("Add sample directory", juce::File(), "*");
    juce::Component::SafePointer<Browser> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe](const juce::FileChooser& selected)
        {
            if (!safe) return; safe->choosing = false; const auto folder = selected.getResult(); if (!folder.isDirectory()) return;
            safe->presenter.addSampleFolder(folder); safe->refresh();
            const auto index = safe->cachedFolders.indexOf(folder.getFullPathName());
            if (index >= 0) safe->locations.setSelectedId(index + 2);
        });
}
void Browser::updateSource()
{
    const auto index = locations.getSelectedId() - 2;
    if (index >= 0 && index < cachedFolders.size())
    { const juce::File folder(cachedFolders[index]); if (folder != directory.getDirectory()) { folderAvailable = folder.isDirectory(); files.deselectAllFiles(); directory.setDirectory(folder, true, true); } }
}
void Browser::refresh()
{
    if (locations.getNumItems() == 0 || cachedFolders != presenter.sampleFolders())
    {
        const auto selected = locations.getSelectedId() - 2;
        const auto previous = selected >= 0 && selected < cachedFolders.size() ? cachedFolders[selected] : juce::String();
        cachedFolders = presenter.sampleFolders(); locations.clear(juce::dontSendNotification); locations.addItem("Project Audio", 1);
        for (int i = 0; i < cachedFolders.size(); ++i)
        { const juce::File folder(cachedFolders[i]); locations.addItem((folder.isDirectory() ? "" : "Missing: ") + folder.getFileName() + " - " + folder.getParentDirectory().getFileName(), i + 2); }
        const auto index = cachedFolders.indexOf(previous); locations.setSelectedId(index >= 0 ? index + 2 : 1, juce::dontSendNotification); updateSource();
    }
    const bool sounds = category == Category::sounds, external = sounds && locations.getSelectedId() > 1;
    locations.setVisible(sounds); removeFolder.setVisible(sounds); removeFolder.setEnabled(locations.getSelectedId() > 1);
    addFolder.setVisible(sounds); search.setVisible(!sounds); files.setVisible(external); list.setVisible(!external);
    const auto selectedRow = list.getSelectedRow();
    const auto selectedItem = selectedRow >= 0 && selectedRow < static_cast<int>(rows.size()) ? rows[static_cast<size_t>(selectedRow)].drag : juce::String();
    rows.clear();
    if (sounds && !external)
        for (const auto& asset : presenter.project().assets) rows.push_back({ (asset.missing ? "! Missing: " : "") + assetName(asset), "asset:" + asset.id, {}, false });
    if (!sounds)
    {
        if (category == Category::instruments && juce::String("Sampler").containsIgnoreCase(search.getText().trim())) rows.push_back({ "Sampler", "device:builtin:sampler", "Built-in", false });
        for (const auto& plugin : presenter.plugins().availablePlugins())
            if (plugin.isInstrument == (category == Category::instruments) && plugin.name.containsIgnoreCase(search.getText().trim()))
                rows.push_back({ plugin.name, "device:" + plugin.createIdentifierString(), plugin.pluginFormatName == "Opentone" ? "Built-in" : plugin.pluginFormatName == "AudioUnit" ? "AU" : plugin.pluginFormatName, plugin.pluginFormatName != "Opentone" });
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b)
        { if (a.format == "Built-in" && b.format != "Built-in") return true; if (a.format != "Built-in" && b.format == "Built-in") return false; return a.name.compareIgnoreCase(b.name) < 0; });
    }
    list.updateContent(); int selection = -1;
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].drag == selectedItem) selection = static_cast<int>(i);
    list.selectRow(selection, true, true);
    label(empty, external ? (folderAvailable ? "" : "Folder unavailable") : "No items");
    empty.setVisible(external ? !folderAvailable : rows.empty());
    resized(); list.repaint(); repaint();
}
int Browser::getNumRows() { return static_cast<int>(rows.size()); }
void Browser::paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected)
{
    if (row < 0 || row >= getNumRows()) return;
    const auto& item = rows[static_cast<size_t>(row)]; if (selected) graphics.fillAll(accent.withAlpha(0.3f));
    graphics.setColour(ink); graphics.setFont(juce::FontOptions(12));
    const int badge = item.format.isEmpty() ? 0 : 48;
    graphics.drawText(item.name, 6, 0, width - badge - 12, height, juce::Justification::centredLeft, true);
    if (badge > 0) { graphics.setFont(juce::FontOptions(10)); graphics.drawText(item.format, width - badge - 4, 0, badge, height, juce::Justification::centredRight); }
}
juce::var Browser::getDragSourceDescription(const juce::SparseSet<int>& selected)
{ return selected.size() > 0 && selected[0] < getNumRows() ? juce::var(rows[static_cast<size_t>(selected[0])].drag) : juce::var(); }
void Browser::selectionChanged()
{ const auto file = files.getSelectedFile(); files.setDragAndDropDescription(file.existsAsFile() ? "sample:" + file.getFullPathName() : juce::String()); }
void Browser::useFile(const juce::File& file)
{ if (file.existsAsFile()) presenter.importFiles({file.getFullPathName()}, presenter.viewState().selectedTrack,
    presenter.viewState().timeSelection ? presenter.viewState().timeSelection->start : presenter.position()); }
void Browser::fileDoubleClicked(const juce::File& file) { useFile(file); }
void Browser::activate(int index)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto item = rows[static_cast<size_t>(index)];
    if (item.drag.startsWith("device:")) presenter.addDevice(item.drag.substring(7), presenter.viewState().selectedTrack);
    else if (const auto* asset = findId(presenter.project().assets, item.drag.substring(6)))
    {
        const auto* track = findId(presenter.project().tracks, presenter.viewState().selectedTrack); if (!track) return;
        const auto duration = static_cast<double>(asset->length) / asset->sampleRate;
        if (track->midi) presenter.loadSampler(track->id, asset->id, 0, duration);
        else presenter.insertClip({ newId(), track->id, asset->id, presenter.viewState().timeSelection ? presenter.viewState().timeSelection->start : presenter.position(), 0, duration }, false);
    }
}
void Browser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) { activate(row); }
void Browser::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu() || row < 0 || row >= getNumRows() || !rows[static_cast<size_t>(row)].plugin) return;
    const auto id = rows[static_cast<size_t>(row)].drag.substring(7); juce::PopupMenu menu;
    menu.addItem(1, "Assign to Instruments", true, category == Category::instruments);
    menu.addItem(2, "Assign to Effects", true, category == Category::effects);
    juce::WeakReference<EditorPresenter> owner(&presenter);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&list).withMousePosition(), [owner, id](int choice)
    { if (owner && choice != 0) owner->plugins().assignCategory(id, choice == 1); });
}
bool Browser::keyPressed(const juce::KeyPress& key)
{ return key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey; }
void Browser::paint(juce::Graphics& graphics) { graphics.fillAll(panel); graphics.setColour(line); graphics.drawRect(getLocalBounds()); }
void Browser::resized()
{
    auto area = getLocalBounds().reduced(7); title.setBounds(area.removeFromTop(23)); categories.setBounds(area.removeFromTop(78)); area.removeFromTop(5);
    auto source = area.removeFromTop(26); if (category == Category::sounds) { removeFolder.setBounds(source.removeFromRight(24)); source.removeFromRight(3); locations.setBounds(source); }
    else search.setBounds(source);
    area.removeFromTop(5); if (category == Category::sounds) { auto footer = area.removeFromBottom(25); addFolder.setBounds(footer); area.removeFromBottom(5); }
    list.setBounds(area); files.setBounds(area); empty.setBounds(area.withHeight(25));
}
}
