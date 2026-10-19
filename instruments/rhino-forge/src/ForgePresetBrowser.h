#pragma once

#include "ForgeProcessor.h"
#include "../ui/ForgeControls.h"
#include <map>
#include <set>

// The preset browser: LOAD opens it over the modules and closes it again, SAVE
// opens it with the save form in its info card.
//
// Down the left, what the loaded patch says about itself and, for a converted
// one, everything the conversion could not carry; then a search, the nine
// style chips and the folders: everything in the bank, the favourites, the
// bank's own tree, and the Serum 2 and Vital libraries this machine has. On the
// right, the presets of the folder chosen, as a table that sorts by any column.
// A click loads, so the keyboard below auditions while the list is read; the
// arrows walk the list loading as they go.
//
// The bank is the player's to arrange: folders are made, renamed and deleted
// from the folder list's menu, presets renamed, moved, deleted or dragged onto
// a folder. A Serum or Vital preset dragged into the bank, or copied there from
// its menu, is converted into a Forge preset on the way. Deleting sends to the
// recycle bin.
//
// Opaque and buffered: the panel under it repaints at 24 Hz, and this repaints
// only when something in it changes. See ForgePresetBrowserInternal.h for the
// pieces and the split across translation units.
namespace rhino::forge
{
class PresetBrowser final : public juce::Component, private juce::TextEditor::Listener
{
public:
    explicit PresetBrowser(Processor&);
    ~PresetBrowser() override;

    // Asks the panel to take the browser away: the close button, Escape, a
    // double-click on a preset.
    std::function<void()> onClose;
    // Tells the panel a patch has arrived, so it re-reads the modules it shows
    // and says on its status line how it went.
    std::function<void(const juce::Result&, const juce::File&)> onPresetLoaded;
    // A message for the panel's status line.
    std::function<void(const juce::String&, bool failed)> announce;

    // Rescans the bank and the libraries and shows the folder the loaded
    // patch came from.
    void openToBrowse();
    // The same, with the save form in the info card, filled from the patch.
    void openToSave();
    bool isSaving() const { return saving; }
    // The preset before or after the one loaded, in the list as it is
    // filtered and sorted, loaded. The arrows either side of the name.
    void step(int delta);

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    // The pieces, defined in ForgePresetBrowserInternal.h.
    class FolderList;
    class PresetTable;
    class InfoCard;

    // One line of the folder list.
    struct FolderRow
    {
        enum class Kind { all, favourites, folder, library };
        Kind kind = Kind::folder;
        juce::String caption;
        juce::File folder;
        int depth = 0;
        // Whether a preset may be written here: the bank's rows are, a
        // library's are not.
        bool writable = false;
        // Whether this row has folders under it, which is what puts a
        // disclosure arrow in front of it.
        bool hasChildren = false;
    };

    // What the views read and the actions they ask for. Public to the views
    // in the same files rather than to the panel, which only opens, closes and
    // steps.
    Processor& processor;
    std::vector<FolderRow> folderRows;
    int chosenFolder = 0;
    std::vector<PresetEntry> listed;
    std::vector<int> shown;
    juce::File highlighted;
    enum class Column { favourite, name, style, author, date };
    Column sortColumn = Column::name;
    bool sortAscending = true;
    juce::StringArray favourites;
    std::set<juce::String> collapsed;
    // A preset being dragged towards a folder, and the folder row under it.
    juce::File dragging;
    int dropRow = -1;

    const FolderRow* chosenRow() const;
    bool isFavourite(const juce::File&) const;
    void toggleFavourite(const juce::File&);
    void chooseFolder(int row);
    void toggleCollapsed(int row);
    void loadPreset(const juce::File&);
    void sortBy(Column);
    void showPresetMenu(const juce::File&);
    void showFolderMenu(int row);
    void beginRename(const juce::File&, juce::Component& over, juce::Rectangle<int> where);
    void dragTo(juce::Point<int> inBrowser);
    void dropAt(juce::Point<int> inBrowser);
    // Where the save form will write: the bank folder chosen, or the folder
    // the loaded patch came from when that is in the bank, or the bank.
    juce::File saveFolder() const;

private:
    bool saving = false;
    // Asked once to replace a file of the same name; a second SAVE does it.
    juce::File confirmReplace;
    // Every folder's presets as last read, by path, until the next open or the
    // next change to the files.
    std::map<juce::String, std::vector<PresetEntry>> scanned;

    std::unique_ptr<InfoCard> info;
    std::unique_ptr<FolderList> folders;
    std::unique_ptr<PresetTable> table;
    juce::TextEditor search;
    std::vector<std::unique_ptr<ui::ToggleChip>> styleChips;
    ui::PresetButton closeButton {"CLOSE"}, openButton {"OPEN FILE"}, initButton {"INIT PATCH"};
    ui::PresetButton previousButton {"<"}, nextButton {">"};
    ui::PresetButton saveButton {"SAVE"}, cancelButton {"CANCEL"};
    juce::TextEditor nameField, authorField, notesField;
    ui::PresetButton styleField {"STYLE"};
    juce::String chosenStyle;
    juce::TextEditor renameField;
    juce::File renaming;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> topBar, leftColumn, infoArea, foldersTitle;

    void rebuildFolders();
    void relist();
    void refilter();
    void revealHighlighted();
    void showSaveForm(bool show);
    void commitSave();
    void chooseStyleForSave();
    void showBrowserMenu();
    void chooseFileToOpen();
    void commitRename();
    void cancelRename();
    void movePreset(const juce::File& preset, const juce::File& folder);
    void createFolderIn(const juce::File& parent);
    void deleteItem(const juce::File&);
    void reportFailure(const juce::Result&);
    void rememberMove(const juce::File& from, const juce::File& to);
    juce::String folderCaption(const juce::File&) const;

    void textEditorTextChanged(juce::TextEditor&) override;
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override;
    void textEditorFocusLost(juce::TextEditor&) override;
};
}
