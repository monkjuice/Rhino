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
class PresetBrowser final : public juce::Component, private juce::TextEditor::Listener,
                            private juce::AsyncUpdater
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
    class Lister;

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
    // Whether the folder chosen is still being read, on the lister's thread.
    // The table shows what the folder listed last time meanwhile, or says
    // that it is reading when there is nothing yet.
    bool listing = false;
    // Delivers any listing the lister has finished now, rather than on the
    // message loop's next turn: for a test or a tool that runs no loop. The
    // loop itself delivers them the same way, through the AsyncUpdater.
    void takeListings() { handleUpdateNowIfNeeded(); }

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
    // Every folder's presets as last read, by path. Opening the browser marks
    // them stale rather than throwing them away: the next listing of a stale
    // folder walks it again and opens only the files whose dates moved, so a
    // library of six hundred costs its first open and a few milliseconds
    // after. A change the browser makes to the files itself clears them.
    struct Scan
    {
        std::vector<PresetEntry> entries;
        bool fresh = true;
    };
    std::map<juce::String, Scan> scanned;
    // The number of the listing last asked for, so one that lands after the
    // folder has been changed again is kept but not shown.
    int listRequest = 0;
    void listArrived(int request, const juce::String& key, std::vector<PresetEntry> entries);
    // What the lister has finished and the message thread has not yet taken.
    // The lister pushes under the lock and triggers the update; nothing on
    // the lister's thread touches the browser otherwise.
    struct Arrival
    {
        int request = 0;
        juce::String key;
        std::vector<PresetEntry> entries;
    };
    juce::CriticalSection arrivalsLock;
    std::vector<Arrival> arrivals;
    void handleAsyncUpdate() override;

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
    // The plate, the wells and the titles, which are the same from one frame
    // to the next and were most of what a repaint cost: drawn once into an
    // image keyed on the size and the form, and blitted after. paint() adds
    // the little that moves over it.
    juce::Image chrome;
    juce::String chromeState;
    void paintChrome(juce::Graphics&);

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
    // The thread a folder is listed on. Last, so it is stopped before anything
    // it could still be handing a listing to is gone.
    std::unique_ptr<Lister> lister;
};
}
