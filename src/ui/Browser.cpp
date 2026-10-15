// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/Browser.h"
#include "ui/UiTheme.h"
namespace opentone
{
class Browser::Folder final : public juce::TreeViewItem
{
public:
    Folder(Browser& browser, juce::String text, Category kind, juce::String path = {}, bool section = false)
        : owner(browser), name(std::move(text)), folder(std::move(path)), category(kind), heading(section) {}
    bool mightContainSubItems() override { return name.isEmpty(); }
    bool canBeSelected() const override { return !heading; }
    int getItemHeight() const override { return heading ? 27 : 21; }
    juce::String getUniqueName() const override { return name + folder; }
    juce::String getTooltip() override { return name; }
    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (isSelected()) { g.setColour(accent); g.fillRect(0, 0, width, height); }
        g.setColour(isSelected() && !heading ? uiTone(UiTone::selectionText) : ink.withAlpha(heading ? 0.55f : 1.0f)); g.setFont(juce::FontOptions(11));
        if (heading && owner.getWidth() < 220) { g.drawHorizontalLine(height / 2, 4, static_cast<float>(width - 4)); return; }
        if (!heading)
        {
            if (folder.isNotEmpty() || name == "Current Project")
            { g.fillRect(5, 6, 6, 3); g.drawRect(5, 8, 12, 8); }
            else if (category == Category::sounds)
                for (int i = 0; i < 5; ++i) g.drawVerticalLine(6 + i * 2, static_cast<float>(8 - i % 3), static_cast<float>(13 + i % 3));
            else if (category == Category::instruments)
            { g.drawRect(5, 6, 12, 10); for (int i = 1; i < 4; ++i) g.drawVerticalLine(5 + i * 3, 7, 15); }
            else if (category == Category::effects)
            { g.drawLine(5, 15, 8, 7); g.drawLine(8, 7, 12, 15); g.drawLine(12, 15, 17, 7); }
            else { g.drawRect(6, 8, 10, 7); g.drawVerticalLine(8, 5, 8); g.drawVerticalLine(14, 5, 8); }
        }
        if (owner.getWidth() >= 220)
            g.drawText(name, heading ? 6 : 23, 0, width - (heading ? 10 : 27), height, juce::Justification::centredLeft, true);
    }
    void itemSelectionChanged(bool selected) override
    { if (selected && !heading && name.isNotEmpty()) owner.selectCategory(category, folder); }
    void itemClicked(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu() || folder.isEmpty()) return;
        juce::PopupMenu menu; menu.addItem(1, "Remove from Places");
        juce::Component::SafePointer<Browser> safe(&owner); const auto path = folder;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&owner.categories).withMousePosition(), [safe, path](int choice)
        { if (safe && choice == 1) { safe->presenter.removeSampleFolder(juce::File(path)); safe->refresh(); } });
    }
private:
    Browser& owner; juce::String name, folder; Category category; bool heading;
};
class Browser::Preview final : public juce::Component, private juce::ChangeListener
{
public:
    Preview() : thumbnail(128, formats, cache)
    { formats.registerBasicFormats(); thumbnail.addChangeListener(this); setInterceptsMouseClicks(false, false); }
    ~Preview() override { thumbnail.removeChangeListener(this); }
    void setFile(const juce::File& next)
    {
        if (next == file) return;
        file = next; thumbnail.clear();
        if (file.existsAsFile()) thumbnail.setSource(new juce::FileInputSource(file));
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(panel); g.setColour(line); g.drawHorizontalLine(0, 0, static_cast<float>(getWidth()));
        if (file == juce::File()) return;
        g.setColour(ink); g.setFont(juce::FontOptions(10));
        g.drawText(file.getFileName(), 6, 3, getWidth() - 70, 14, juce::Justification::centredLeft, true);
        if (thumbnail.getTotalLength() > 0)
        {
            g.drawText(juce::String(thumbnail.getTotalLength(), 2) + " s", getWidth() - 65, 3, 59, 14, juce::Justification::centredRight);
            g.setColour(primary); thumbnail.drawChannels(g, getLocalBounds().reduced(6).withTrimmedTop(15), 0, thumbnail.getTotalLength(), 0.9f);
        }
    }
private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
    juce::File file; juce::AudioFormatManager formats; juce::AudioThumbnailCache cache {4}; juce::AudioThumbnail thumbnail;
};
Browser::Browser(EditorPresenter& owner)
    : presenter(owner), root(std::make_unique<Folder>(*this, juce::String(), Category::sounds)), preview(std::make_unique<Preview>())
{
    for (auto* child : std::initializer_list<juce::Component*> { &title, &contentsTitle, &empty, &categories, &search, &addFolder, &list, &files, preview.get() }) addAndMakeVisible(child);
    label(title, "Library"); list.setRowHeight(21); files.setItemHeight(21); files.setMultiSelectEnabled(false); files.addListener(this);
    categories.setRootItem(root.get()); categories.setRootItemVisible(false); categories.setMultiSelectEnabled(false); categories.setIndentSize(0);
    addFolder.setTitle("Link a sample directory without copying its files"); addFolder.onClick = [this] { chooseFolder(); };
    search.setTextToShowWhenEmpty("Search", ink.withAlpha(0.5f)); search.setFont(juce::FontOptions(11)); search.setIndents(7, 3);
    search.onTextChange = [this] { refresh(); };
    directoryThread.startThread(); refresh(); root->getSubItem(1)->setSelected(true, true);
}
Browser::~Browser()
{
    ++*searchGeneration; searchPool.removeAllJobs(true, -1);
    files.removeListener(this); categories.setRootItem(nullptr); directoryThread.stopThread(-1);
}
void Browser::selectCategory(Category next, juce::String folder)
{ category = next; selectedFolder = std::move(folder); preview->setFile({}); updateSource(); refresh(); }
void Browser::rebuildPlaces()
{
    root->clearSubItems();
    root->addSubItem(new Folder(*this, "Library", Category::sounds, {}, true));
    root->addSubItem(new Folder(*this, "Sounds", Category::sounds));
    root->addSubItem(new Folder(*this, "Instruments", Category::instruments));
    root->addSubItem(new Folder(*this, "Audio Effects", Category::effects));
    root->addSubItem(new Folder(*this, "Plug-ins", Category::plugins));
    root->addSubItem(new Folder(*this, "Places", Category::sounds, {}, true));
    root->addSubItem(new Folder(*this, "Current Project", Category::sounds));
    for (const auto& path : cachedFolders)
    {
        const juce::File folder(path);
        root->addSubItem(new Folder(*this, (folder.isDirectory() ? "" : "Missing: ") + folder.getFileName(), Category::sounds, path));
    }
    root->setOpen(true);
    const auto selected = selectedFolder.isEmpty() ? category == Category::sounds ? 1 : category == Category::instruments ? 2 : category == Category::effects ? 3 : 4 : cachedFolders.indexOf(selectedFolder) + 7;
    jassert(selected >= 1 && selected < root->getNumSubItems());
    root->getSubItem(selected)->setSelected(true, true, juce::dontSendNotification);
}
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
            if (index >= 0) safe->root->getSubItem(index + 7)->setSelected(true, true);
        });
}
void Browser::updateSource()
{
    folderAvailable = selectedFolder.isNotEmpty() && juce::File(selectedFolder).isDirectory();
    if (folderAvailable && juce::File(selectedFolder) != directory.getDirectory())
    { files.deselectAllFiles(); directory.setDirectory(juce::File(selectedFolder), true, true); }
}
void Browser::searchFiles()
{
    const auto query = search.getText().trim(); const auto path = category == Category::sounds ? selectedFolder : juce::String();
    if (query == searchQuery && path == searchDirectory) return;
    searchQuery = query; searchDirectory = path; foundFiles.clear();
    const auto generation = ++*searchGeneration; searching = path.isNotEmpty() && query.isNotEmpty() && folderAvailable;
    if (!searching) return;
    const auto token = searchGeneration; juce::Component::SafePointer<Browser> safe(this);
    searchPool.addJob(std::function<void()>([token, generation, safe, path, query]
    {
        if (token->load() != generation) return;
        juce::Array<juce::File> found;
        for (const auto& entry : juce::RangedDirectoryIterator(juce::File(path), true, "*.wav;*.aif;*.aiff;*.flac;*.mp3", juce::File::findFiles, juce::File::FollowSymlinks::noCycles))
        {
            if (token->load() != generation) return;
            if (entry.getFile().getFileName().containsIgnoreCase(query)) found.add(entry.getFile());
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.getFullPathName().compareIgnoreCase(b.getFullPathName()) < 0; });
        juce::MessageManager::callAsync([token, generation, safe, found = std::move(found)]() mutable
        { if (token->load() == generation && safe) { safe->foundFiles = std::move(found); safe->searching = false; safe->refresh(); } });
    }));
}
void Browser::refresh()
{
    if (root->getNumSubItems() == 0 || cachedFolders != presenter.sampleFolders())
    {
        cachedFolders = presenter.sampleFolders();
        if (selectedFolder.isNotEmpty() && !cachedFolders.contains(selectedFolder)) selectedFolder.clear();
        rebuildPlaces();
    }
    updateSource(); searchFiles();
    const bool sounds = category == Category::sounds, external = sounds && selectedFolder.isNotEmpty();
    const auto query = search.getText().trim(); const bool tree = external && query.isEmpty();
    files.setVisible(tree && folderAvailable); list.setVisible(!tree); preview->setVisible(sounds);
    const auto selectedRow = list.getSelectedRow();
    const auto selectedItem = selectedRow >= 0 && selectedRow < static_cast<int>(rows.size()) ? rows[static_cast<size_t>(selectedRow)].drag : juce::String();
    rows.clear();
    if (brandQuery != query) { brandQuery = query; collapsedSearchBrands.clear(); }
    if (sounds && !external)
        for (const auto& asset : presenter.project().assets)
            if (assetName(asset).containsIgnoreCase(query)) rows.push_back({ (asset.missing ? "! Missing: " : "") + assetName(asset), "asset:" + asset.id, {}, false });
    if (external && !tree)
        for (const auto& file : foundFiles) rows.push_back({file.getFileName(), "sample:" + file.getFullPathName(), {}, false});
    if (!sounds)
    {
        if (category == Category::instruments && juce::String("Sampler").containsIgnoreCase(query)) rows.push_back({ "Sampler", "device:builtin:sampler", {}, false });
        std::map<juce::String, std::vector<Row>> brands;
        for (const auto& plugin : presenter.plugins().availablePlugins())
        {
            const bool native = plugin.pluginFormatName != "Opentone";
            const bool belongs = category == Category::plugins ? native : plugin.isInstrument == (category == Category::instruments);
            const auto brand = plugin.manufacturerName.trim().isEmpty() ? juce::String("Unknown Manufacturer") : plugin.manufacturerName.trim();
            if (belongs && (plugin.name.containsIgnoreCase(query) || (native && brand.containsIgnoreCase(query))))
            {
                Row item {plugin.name, "device:" + plugin.createIdentifierString(), !native ? juce::String() : plugin.pluginFormatName == "AudioUnit" ? "AU" : plugin.pluginFormatName, native, native ? brand : juce::String()};
                if (native) brands[brand].push_back(std::move(item)); else rows.push_back(std::move(item));
            }
        }
        const auto byName = [](const Row& a, const Row& b) { return a.name.compareIgnoreCase(b.name) < 0; };
        std::sort(rows.begin(), rows.end(), byName);
        for (auto& [brand, plugins] : brands)
        {
            jassert(brand.isNotEmpty() && !plugins.empty());
            rows.push_back({brand, "brand:" + brand, {}, false, brand, true});
            if (query.isEmpty() ? expandedBrands.contains(brand) : !collapsedSearchBrands.contains(brand))
            {
                std::sort(plugins.begin(), plugins.end(), byName);
                rows.insert(rows.end(), plugins.begin(), plugins.end());
            }
        }
    }
    list.updateContent(); int selection = -1;
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].drag == selectedItem) selection = static_cast<int>(i);
    list.selectRow(selection, true, true);
    label(empty, external && !folderAvailable ? "Folder unavailable" : searching ? "Searching..." : query.isEmpty() ? "No items" : "No results");
    empty.setVisible(external && !folderAvailable ? true : tree ? false : rows.empty());
    label(contentsTitle, external ? juce::File(selectedFolder).getFileName() : sounds ? "Sounds" : category == Category::instruments ? "Instruments" : category == Category::effects ? "Audio Effects" : "Plug-ins");
    resized(); list.repaint(); repaint();
}
int Browser::getNumRows() { return static_cast<int>(rows.size()); }
void Browser::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= getNumRows()) return;
    const auto& item = rows[static_cast<size_t>(row)]; if (selected) g.fillAll(accent);
    g.setColour(selected ? uiTone(UiTone::selectionText) : ink); g.setFont(juce::FontOptions(11));
    const int indent = !item.folder && item.brand.isNotEmpty() ? 14 : 0;
    if (item.folder)
    {
        const bool open = search.getText().trim().isEmpty() ? expandedBrands.contains(item.brand) : !collapsedSearchBrands.contains(item.brand);
        juce::Path arrow;
        if (open) arrow.addTriangle(5, 8, 13, 8, 9, 13); else arrow.addTriangle(6, 6, 6, 14, 11, 10);
        g.fillPath(arrow); g.drawRect(17, 7, 10, 8);
    }
    else if (item.drag.startsWith("device:")) { g.drawRect(5 + indent, 6, 11, 10); g.drawHorizontalLine(10, static_cast<float>(7 + indent), static_cast<float>(14 + indent)); }
    else for (int i = 0; i < 5; ++i) g.drawVerticalLine(6 + i * 2, static_cast<float>(8 - i % 3), static_cast<float>(13 + i % 3));
    const int badge = item.format.isEmpty() ? 0 : 35;
    const int textX = item.folder ? 33 : 23 + indent;
    g.drawText(item.name, textX, 0, width - badge - textX - 4, height, juce::Justification::centredLeft, true);
    if (badge > 0) { g.setColour((selected ? uiTone(UiTone::selectionText) : ink).withAlpha(0.6f)); g.setFont(juce::FontOptions(9)); g.drawText(item.format, width - badge - 4, 0, badge, height, juce::Justification::centredRight); }
}
juce::var Browser::getDragSourceDescription(const juce::SparseSet<int>& selected)
{ return selected.size() > 0 && selected[0] >= 0 && selected[0] < getNumRows() && !rows[static_cast<size_t>(selected[0])].folder ? juce::var(rows[static_cast<size_t>(selected[0])].drag) : juce::var(); }
void Browser::selectedRowsChanged(int row)
{
    juce::File file;
    if (row >= 0 && row < getNumRows())
    {
        const auto& drag = rows[static_cast<size_t>(row)].drag;
        if (drag.startsWith("sample:")) file = juce::File(drag.substring(7));
        else if (drag.startsWith("asset:")) if (const auto* asset = findId(presenter.project().assets, drag.substring(6)))
            if (!asset->missing) file = presenter.mediaDirectory().getChildFile(asset->path);
    }
    preview->setFile(file);
}
void Browser::selectionChanged()
{ const auto file = files.getSelectedFile(); files.setDragAndDropDescription(file.existsAsFile() ? "sample:" + file.getFullPathName() : juce::String()); preview->setFile(file.existsAsFile() ? file : juce::File()); }
void Browser::useFile(const juce::File& file)
{ if (file.existsAsFile()) presenter.importFiles({file.getFullPathName()}, presenter.viewState().selectedTrack,
    presenter.viewState().timeSelection ? presenter.viewState().timeSelection->start : presenter.position()); }
void Browser::fileDoubleClicked(const juce::File& file) { useFile(file); }
void Browser::activate(int index)
{
    if (index < 0 || index >= getNumRows()) return;
    const auto item = rows[static_cast<size_t>(index)];
    if (item.folder) { toggleBrand(item.brand); return; }
    if (item.drag.startsWith("device:")) presenter.addDevice(item.drag.substring(7), presenter.viewState().selectedTrack);
    else if (item.drag.startsWith("sample:")) useFile(juce::File(item.drag.substring(7)));
    else if (const auto* asset = findId(presenter.project().assets, item.drag.substring(6)))
    {
        const auto* track = findId(presenter.project().tracks, presenter.viewState().selectedTrack); if (!track) return;
        const auto duration = static_cast<double>(asset->length) / asset->sampleRate;
        if (track->midi) presenter.loadSampler(track->id, asset->id, 0, duration);
        else presenter.insertClip({ newId(), track->id, asset->id, presenter.viewState().timeSelection ? presenter.viewState().timeSelection->start : presenter.position(), 0, duration }, false);
    }
}
void Browser::toggleBrand(const juce::String& brand)
{
    if (search.getText().trim().isEmpty())
    { if (expandedBrands.contains(brand)) expandedBrands.removeString(brand); else expandedBrands.add(brand); }
    else
    { if (collapsedSearchBrands.contains(brand)) collapsedSearchBrands.removeString(brand); else collapsedSearchBrands.add(brand); }
    refresh();
}
void Browser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{ if (row >= 0 && row < getNumRows() && !rows[static_cast<size_t>(row)].folder) activate(row); }
void Browser::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
    if (row < 0 || row >= getNumRows()) return;
    if (rows[static_cast<size_t>(row)].folder)
    { if (event.mods.isLeftButtonDown() && event.getNumberOfClicks() == 1) toggleBrand(rows[static_cast<size_t>(row)].brand); return; }
    if (!event.mods.isPopupMenu() || !rows[static_cast<size_t>(row)].plugin) return;
    const auto id = rows[static_cast<size_t>(row)].drag.substring(7); bool instrument = false;
    for (const auto& plugin : presenter.plugins().availablePlugins()) if (plugin.createIdentifierString() == id) { instrument = plugin.isInstrument; break; }
    juce::PopupMenu menu; menu.addItem(1, "Assign to Instruments", true, instrument); menu.addItem(2, "Assign to Effects", true, !instrument);
    juce::WeakReference<EditorPresenter> owner(&presenter);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&list).withMousePosition(), [owner, id](int choice)
    { if (owner && choice != 0) owner->plugins().assignCategory(id, choice == 1); });
}
bool Browser::keyPressed(const juce::KeyPress& key)
{
    const auto row = list.getSelectedRow();
    if (list.hasKeyboardFocus(true) && row >= 0 && row < getNumRows() && rows[static_cast<size_t>(row)].folder
        && (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey))
    {
        const auto brand = rows[static_cast<size_t>(row)].brand;
        const bool open = search.getText().trim().isEmpty() ? expandedBrands.contains(brand) : !collapsedSearchBrands.contains(brand);
        if (open != (key.getKeyCode() == juce::KeyPress::rightKey)) toggleBrand(brand);
        return true;
    }
    return key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey;
}
void Browser::paint(juce::Graphics& g)
{
    g.fillAll(panel); g.setColour(line); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4, 1);
    g.drawVerticalLine(getWidth() < 220 ? 33 : 128, 32, static_cast<float>(getHeight() - 4));
    g.setColour(ink.withAlpha(0.55f)); g.drawEllipse(8, 12, 8, 8, 1); g.drawLine(15, 19, 19, 23, 1);
}
void Browser::resized()
{
    auto area = getLocalBounds().reduced(4); auto top = area.removeFromTop(27); top.removeFromLeft(19); search.setBounds(top.reduced(1, 2));
    title.setVisible(false); const bool compact = getWidth() < 220;
    auto sidebar = area.removeFromLeft(compact ? 26 : 121); area.removeFromLeft(5);
    addFolder.setButtonText(compact ? "+" : "Add Folder..."); categories.repaint();
    addFolder.setBounds(sidebar.removeFromBottom(23)); sidebar.removeFromBottom(3); categories.setBounds(sidebar);
    if (category == Category::sounds) preview->setBounds(area.removeFromBottom(62));
    contentsTitle.setBounds(area.removeFromTop(22));
    list.setBounds(area); files.setBounds(area); empty.setBounds(area.withHeight(25));
}
}
