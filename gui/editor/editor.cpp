/*****************************************************************************
    NumeRe: Framework fuer Numerische Rechnungen
    Copyright (C) 2019  Erik Haenel et al.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/



#define CRTDBG_MAP_ALLOC
#include <stdlib.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#else
#define _ASSERT(expr) ((void)0)

#define _ASSERTE(expr) ((void)0)
#endif

#include "../../common/CommonHeaders.h"
#include "../../kernel/core/ui/language.hpp"
#include "../../kernel/core/utils/tools.hpp"

#include <wx/datetime.h>
#include <wx/stdpaths.h>
#include <vector>
#include <string>
#include <set>
#include <memory>

#include "editor.h"
#include "../NumeReWindow.h"
#include "../NumeReNotebook.h"
#include "codeanalyzer.hpp"
#include "searchcontroller.hpp"
#include "codeformatter.hpp"

#include "../../common/datastructures.h"
#include "../../common/Options.h"
#include "../../common/DebugEvent.h"
#include "../../common/ProjectInfo.h"
#include "../../common/debug.h"
#include "../../common/vcsmanager.hpp"
#include "../../common/filerevisions.hpp"
#include "../dialogs/renamesymbolsdialog.hpp"
//#include "../../common/fixvsbug.h"
#include "../globals.hpp"

#define MARGIN_FOLD 3
#define HIGHLIGHT 25
#define HIGHLIGHT_DBLCLK 26
#define HIGHLIGHT_MATCHING_BRACE 6
#define HIGHLIGHT_STRIKETHROUGH 7
#define HIGHLIGHT_MATCHING_BLOCK 8
#define HIGHLIGHT_NOT_MATCHING_BLOCK 9
#define HIGHLIGHT_DIFFERENCES 10
#define HIGHLIGHT_DIFFERENCE_SOURCE 11
#define HIGHLIGHT_ANNOTATION 12
#define HIGHLIGHT_LOCALVARIABLES 13
#define ANNOTATION_NOTE 22
#define ANNOTATION_WARN 23
#define ANNOTATION_ERROR 24

#define SEMANTICS_VAR 1
#define SEMANTICS_STRING 2
#define SEMANTICS_NUM 4
#define SEMANTICS_FUNCTION 8

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


BEGIN_EVENT_TABLE(NumeReEditor, wxStyledTextCtrl)
	EVT_STC_CHARADDED	(-1, NumeReEditor::OnChar)
	EVT_STC_MODIFIED	(-1, NumeReEditor::OnEditorModified)
	EVT_KEY_DOWN        (NumeReEditor::OnKeyDn)
	EVT_KEY_UP          (NumeReEditor::OnKeyRel)
	EVT_LEFT_DOWN       (NumeReEditor::OnMouseDn)
	EVT_LEFT_UP         (NumeReEditor::OnMouseUp)
	EVT_RIGHT_DOWN		(NumeReEditor::OnRightClick)
	EVT_LEFT_DCLICK		(NumeReEditor::OnMouseDblClk)
	EVT_MOUSE_CAPTURE_LOST(NumeReEditor::OnMouseCaptureLost)
	EVT_ENTER_WINDOW    (NumeReEditor::OnEnter)
	EVT_LEAVE_WINDOW    (NumeReEditor::OnLeave)
	EVT_KILL_FOCUS      (NumeReEditor::OnLoseFocus)
	EVT_STC_DWELLSTART  (-1, NumeReEditor::OnMouseDwell)
	EVT_STC_MARGINCLICK (-1, NumeReEditor::OnMarginClick)
	EVT_STC_DRAG_OVER   (-1, NumeReEditor::OnDragOver)
	EVT_STC_SAVEPOINTREACHED (-1, NumeReEditor::OnSavePointReached)
	EVT_STC_SAVEPOINTLEFT (-1, NumeReEditor::OnSavePointLeft)
	EVT_MENU			(ID_DEBUG_ADD_BREAKPOINT, NumeReEditor::OnAddBreakpoint)
	EVT_MENU			(ID_DEBUG_REMOVE_BREAKPOINT, NumeReEditor::OnRemoveBreakpoint)
	EVT_MENU			(ID_DEBUG_CLEAR_ALL_BREAKPOINTS, NumeReEditor::OnClearBreakpoints)
	EVT_MENU			(ID_BOOKMARK_ADD, NumeReEditor::OnAddBookmark)
	EVT_MENU			(ID_BOOKMARK_REMOVE, NumeReEditor::OnRemoveBookmark)
	EVT_MENU			(ID_BOOKMARK_CLEAR, NumeReEditor::OnClearBookmarks)
	EVT_MENU			(ID_DEBUG_DISPLAY_SELECTION, NumeReEditor::OnDisplayVariable)
	EVT_MENU			(ID_FIND_PROCEDURE, NumeReEditor::OnFindProcedure)
	EVT_MENU			(ID_FIND_INCLUDE, NumeReEditor::OnFindInclude)
	EVT_MENU            (ID_UPPERCASE, NumeReEditor::OnChangeCase)
	EVT_MENU            (ID_LOWERCASE, NumeReEditor::OnChangeCase)
	EVT_MENU            (ID_FOLD_CURRENT_BLOCK, NumeReEditor::OnFoldCurrentBlock)
	EVT_MENU            (ID_HIDE_SELECTION, NumeReEditor::OnHideSelection)
	EVT_MENU            (ID_MENU_HELP_ON_ITEM, NumeReEditor::OnHelpOnSelection)
	EVT_MENU			(ID_DEBUG_RUNTOCURSOR, NumeReEditor::OnRunToCursor)
	EVT_MENU            (ID_RENAME_SYMBOLS, NumeReEditor::OnRenameSymbols)
	EVT_MENU            (ID_ABSTRAHIZE_SECTION, NumeReEditor::OnAbstrahizeSection)
	EVT_IDLE            (NumeReEditor::OnIdle)
END_EVENT_TABLE()

int CompareInts(int n1, int n2)
{
	return n1 - n2;
}


extern Language _guilang;
using namespace std;

/////////////////////////////////////////////////
/// \brief Editor constructor.
///
/// \param mframe NumeReWindow*
/// \param options Options*
/// \param project ProjectInfo*
/// \param parent wxWindow*
/// \param id wxWindowID
/// \param __syntax NumeReSyntax*
/// \param __terminal wxTerm*
/// \param pos const wxPoint&
/// \param size const wxSize&
/// \param style long
/// \param name const wxString&
///
/////////////////////////////////////////////////
NumeReEditor::NumeReEditor(NumeReWindow* mframe, Options* options, ProjectInfo* project, wxWindow* parent, wxWindowID id,
                           NumeReSyntax* __syntax, wxTerm* __terminal, const wxPoint& pos, const wxSize& size, long style, const wxString& name) :
                               wxStyledTextCtrl(parent, id, pos, size, style, name)
{
	defaultPage = false;
	m_mainFrame = mframe;
	m_options = options;
	m_project = project;
	m_project->AddEditor(this);
	m_analyzer = new CodeAnalyzer(this, options);
	m_search = new SearchController(this, __terminal);
	m_formatter = new CodeFormatter(this);
	m_duplicateCode = nullptr;
	m_nCallTipStart = 0;
	m_modificationHappened = false;
	m_nDuplicateCodeLines = 6;

	m_watchedString = "";
	m_dblclkString = "";

	m_nEditorSetting = 0;
	m_fileType = FILE_NOTYPE;

	m_bLoadingFile = false;
	m_bLastSavedRemotely = true;
	m_bHasBeenCompiled = false;
	m_PopUpActive = false;

	m_fileNameAndPath.Assign(wxEmptyString);

	m_lastRightClick.x = -1;
	m_lastRightClick.y = -1;
	_syntax = __syntax;
	m_terminal = __terminal;
	m_dragging = false;

	m_nFirstLine = m_nLastLine = 0;
	m_nDuplicateCodeFlag = 0;
	m_procedureViewer = nullptr;

	Bind(wxEVT_THREAD, &NumeReEditor::OnThreadUpdate, this);

	this->SetTabWidth(4);
	this->SetIndent(4);
	this->SetUseTabs(true);

	this->SetMultipleSelection(true);
	this->SetVirtualSpaceOptions(wxSTC_SCVS_RECTANGULARSELECTION);
	this->SetAdditionalSelectionTyping(true);
	this->SetMultiPaste(wxSTC_MULTIPASTE_EACH);

	this->SetMarginWidth(0, 40);
	this->SetMarginType(0, wxSTC_MARGIN_NUMBER);

	this->SetMarginWidth(1, 20);
	this->SetMarginType(1, wxSTC_MARGIN_SYMBOL);

	this->SetYCaretPolicy(wxSTC_CARET_SLOP | wxSTC_CARET_STRICT | wxSTC_CARET_EVEN, 1);

	wxFileName f(wxStandardPaths::Get().GetExecutablePath());
	//wxInitAllImageHandlers();
	this->RegisterImage(NumeReSyntax::SYNTAX_COMMAND, wxBitmap(f.GetPath(true) + "icons\\cmd.png", wxBITMAP_TYPE_PNG));
	this->RegisterImage(NumeReSyntax::SYNTAX_FUNCTION, wxBitmap(f.GetPath(true) + "icons\\fnc.png", wxBITMAP_TYPE_PNG));
	this->RegisterImage(NumeReSyntax::SYNTAX_OPTION, wxBitmap(f.GetPath(true) + "icons\\opt.png", wxBITMAP_TYPE_PNG));
	this->RegisterImage(NumeReSyntax::SYNTAX_CONSTANT, wxBitmap(f.GetPath(true) + "icons\\cnst.png", wxBITMAP_TYPE_PNG));
	this->RegisterImage(NumeReSyntax::SYNTAX_SPECIALVAL, wxBitmap(f.GetPath(true) + "icons\\spv.png", wxBITMAP_TYPE_PNG));
	this->RegisterImage(NumeReSyntax::SYNTAX_OPERATOR, wxBitmap(f.GetPath(true) + "icons\\opr.png", wxBITMAP_TYPE_PNG));
	this->RegisterImage(NumeReSyntax::SYNTAX_METHODS, wxBitmap(f.GetPath(true) + "icons\\mthd.png", wxBITMAP_TYPE_PNG));
	this->RegisterImage(NumeReSyntax::SYNTAX_PROCEDURE, wxBitmap(f.GetPath(true) + "icons\\prc.png", wxBITMAP_TYPE_PNG));

	wxFont font = m_options->GetEditorFont();
	this->StyleSetFont(wxSTC_STYLE_DEFAULT, font);

	// Add the characters for procedures to the word char list
	// this->SetWordChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~");

	this->StyleClearAll();

	this->SetMouseDwellTime(500);

	this->EmptyUndoBuffer();
	m_bSetUnsaved = false;
	m_bNewFile = true;
	UpdateSyntaxHighlighting();
	m_bNewFile = false;

	this->MarkerDefine(MARKER_BREAKPOINT, wxSTC_MARK_CIRCLE);
	this->MarkerSetBackground(MARKER_BREAKPOINT, wxColour("red"));

	this->MarkerDefine(MARKER_BOOKMARK, wxSTC_MARK_SMALLRECT);
	this->MarkerSetBackground(MARKER_BOOKMARK, wxColour(192, 0, 64));

	this->MarkerDefine(MARKER_FOCUSEDLINE, wxSTC_MARK_SHORTARROW);
	this->MarkerSetBackground(MARKER_FOCUSEDLINE, wxColour("yellow"));

	this->MarkerDefine(MARKER_MODIFIED, wxSTC_MARK_LEFTRECT);
	this->MarkerSetBackground(MARKER_MODIFIED, wxColour(255, 220, 0));

	this->MarkerDefine(MARKER_SAVED, wxSTC_MARK_LEFTRECT);
	this->MarkerSetBackground(MARKER_SAVED, wxColour("green"));

	this->MarkerDefine(MARKER_SECTION, wxSTC_MARK_ARROWDOWN);
	//this->MarkerSetBackground(MARKER_SECTION, wxColor(192,192,192));
	//this->MarkerSetBackground(MARKER_SECTION, wxColor(255,192,192));
	this->MarkerSetBackground(MARKER_SECTION, m_options->GetSyntaxStyle(Options::COMMENT).foreground);

	this->MarkerDefine(MARKER_HIDDEN, wxSTC_MARK_UNDERLINE);
	this->MarkerSetBackground(MARKER_HIDDEN, wxColour(128, 128, 128));
	this->MarkerDefine(MARKER_HIDDEN_MARGIN, wxSTC_MARK_DOTDOTDOT);
	this->MarkerSetBackground(MARKER_HIDDEN_MARGIN, wxColour(128, 128, 128));

	this->SetMarginSensitive(1, true);

	this->UsePopUp(false);

	this->SetCaretPeriod(m_options->GetCaretBlinkTime());

	m_refactoringMenu = new wxMenu();
	m_refactoringMenu->Append(ID_RENAME_SYMBOLS, _guilang.get("GUI_MENU_EDITOR_RENAME_SYMBOLS"));
	m_refactoringMenu->Append(ID_ABSTRAHIZE_SECTION, _guilang.get("GUI_MENU_EDITOR_ABSTRAHIZE_SECTION"));

	m_popupMenu.Append(ID_MENU_CUT, _guilang.get("GUI_MENU_EDITOR_CUT"));
	m_popupMenu.Append(ID_MENU_COPY, _guilang.get("GUI_MENU_EDITOR_COPY"));
	m_popupMenu.Append(ID_MENU_PASTE, _guilang.get("GUI_MENU_EDITOR_PASTE"));
	m_popupMenu.AppendSeparator();

	m_popupMenu.Append(ID_FOLD_CURRENT_BLOCK, _guilang.get("GUI_MENU_EDITOR_FOLDCURRENTBLOCK"));
	m_popupMenu.Append(ID_HIDE_SELECTION, _guilang.get("GUI_MENU_EDITOR_HIDECURRENTBLOCK"));
	m_popupMenu.AppendSeparator();

	m_popupMenu.Append(ID_DEBUG_ADD_BREAKPOINT, _guilang.get("GUI_MENU_EDITOR_ADDBP"));
	m_popupMenu.Append(ID_DEBUG_REMOVE_BREAKPOINT, _guilang.get("GUI_MENU_EDITOR_REMOVEBP"));
	m_popupMenu.Append(ID_DEBUG_CLEAR_ALL_BREAKPOINTS, _guilang.get("GUI_MENU_EDITOR_CLEARBP"));

	//m_popupMenu.Append(ID_DEBUG_RUNTOCURSOR, "Run to cursor");

	m_popupMenu.AppendSeparator();

	m_popupMenu.Append(ID_BOOKMARK_ADD, _guilang.get("GUI_MENU_EDITOR_ADDBM"));
	m_popupMenu.Append(ID_BOOKMARK_REMOVE, _guilang.get("GUI_MENU_EDITOR_REMOVEBM"));
	m_popupMenu.Append(ID_BOOKMARK_CLEAR, _guilang.get("GUI_MENU_EDITOR_CLEARBM"));

	m_popupMenu.AppendSeparator();

	//m_menuAddWatch = m_popupMenu.Append(ID_DEBUG_WATCH_SELECTION, "Watch selection");
	m_menuFindProcedure = m_popupMenu.Append(ID_FIND_PROCEDURE, _guilang.get("GUI_MENU_EDITOR_FINDPROC", "$procedure"));
	m_menuFindInclude = m_popupMenu.Append(ID_FIND_INCLUDE, _guilang.get("GUI_MENU_EDITOR_FINDINCLUDE", "script"));
	m_menuShowValue = m_popupMenu.Append(ID_DEBUG_DISPLAY_SELECTION, _guilang.get("GUI_MENU_EDITOR_HIGHLIGHT", "selection"), "", wxITEM_CHECK);
	m_menuHelpOnSelection = m_popupMenu.Append(ID_MENU_HELP_ON_ITEM, _guilang.get("GUI_TREE_PUP_HELPONITEM", "..."));
	m_menuRefactoring = m_popupMenu.Append(ID_REFACTORING_MENU, _guilang.get("GUI_MENU_EDITOR_REFACTORING"), m_refactoringMenu);
	m_popupMenu.AppendSeparator();
	m_popupMenu.Append(ID_UPPERCASE, _guilang.get("GUI_MENU_EDITOR_UPPERCASE"));
	m_popupMenu.Append(ID_LOWERCASE, _guilang.get("GUI_MENU_EDITOR_LOWERCASE"));


	int modmask =	wxSTC_MOD_INSERTTEXT
					| wxSTC_MOD_DELETETEXT
					//| wxSTC_MOD_CHANGESTYLE
					| wxSTC_PERFORMED_UNDO
					| wxSTC_PERFORMED_REDO;

	this->SetModEventMask(modmask);
}


//////////////////////////////////////////////////////////////////////////////
///  public destructor ~ChameleonEditor
///  Handles the pseudo-reference counting for the editor's project
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
NumeReEditor::~NumeReEditor()
{
	if (m_project && m_project->IsSingleFile())
	{
		delete m_project;
		m_project = nullptr;
	}
	else if (m_project)
	{
		m_project->RemoveEditor(this);
	}

	if (m_analyzer)
	{
        delete m_analyzer;
	    m_analyzer = nullptr;
	}

	if (m_search)
	{
        delete m_search;
	    m_search = nullptr;
	}

	if (m_formatter)
	{
        delete m_formatter;
	    m_formatter = nullptr;
	}
}


//////////////////////////////////////////////////////////////////////////////
///  public SaveFileLocal
///  Saves the editor's contents with the current filename
///
///  @return bool Whether or not the save succeeded
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
bool NumeReEditor::SaveFileLocal()
{
	return SaveFile(m_fileNameAndPath.GetFullPath());
}


//////////////////////////////////////////////////////////////////////////////
///  public SaveFile
///  Saves the editor's contents with the given filename
///
///  @param  filename const wxString & The filename to save to
///
///  @return bool     Whether or not the save succeeded
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
bool NumeReEditor::SaveFile( const wxString& filename )
{
	// return if no change
	if (!Modified() && filename.IsEmpty())
	{
		return true;
	}

	wxFileName fn(filename);

	// save edit in file and clear undo
	if (!filename.IsEmpty())
	{
		m_simpleFileName = fn.GetFullName();
	}

	VersionControlSystemManager manager(m_mainFrame);
	unique_ptr<FileRevisions> revisions(manager.getRevisions(filename));

	if (revisions.get())
    {
        if (!revisions->getRevisionCount() && wxFileExists(filename))
        {
            wxFile tempfile(filename);
            wxString contents;
            tempfile.ReadAll(&contents);
            revisions->addRevision(contents);
        }
    }
    else if (wxFileExists(filename))
	{
		wxCopyFile(filename, filename + ".backup", true);
	}

	bool bWriteSuccess = false;

	// Write the file depending on its type
	if (m_fileType == FILE_NSCR || m_fileType == FILE_NPRC || filename.find("numere.history") != string::npos)
        bWriteSuccess = SaveNumeReFile(filename);
    else
        bWriteSuccess = SaveGeneralFile(filename);

	// Check the contents of the newly created file
	wxFile filecheck;
	filecheck.Open(filename);

	if (!bWriteSuccess)
	{
	    // if the contents are not matching, restore the backup and signalize that an error occured
		if (wxFileExists(filename + ".backup"))
			wxCopyFile(filename + ".backup", filename, true);
        else if (revisions.get() && revisions->getRevisionCount())
            revisions->restoreRevision(revisions->getRevisionCount()-1, filename);

		return false;
	}
	else if ((m_fileType == FILE_NSCR || m_fileType == FILE_NPRC) && filecheck.Length() != this->GetLength() - countUmlauts(this->GetText().ToStdString()))
	{
        // if the contents are not matching, restore the backup and signalize that an error occured
		if (wxFileExists(filename + ".backup"))
			wxCopyFile(filename + ".backup", filename, true);
        else if (revisions.get() && revisions->getRevisionCount())
            revisions->restoreRevision(revisions->getRevisionCount()-1, filename);

		return false;
	}
	else if ((m_fileType != FILE_NSCR && m_fileType != FILE_NPRC) && !filecheck.Length() && this->GetLength())
    {
		// if the contents are not matching, restore the backup and signalize that an error occured
		if (wxFileExists(filename + ".backup"))
			wxCopyFile(filename + ".backup", filename, true);
        else if (revisions.get() && revisions->getRevisionCount())
            revisions->restoreRevision(revisions->getRevisionCount()-1, filename);

		return false;
    }

    // Add the current text to the revisions, if the saving process was
    // successful
    if (revisions.get() && m_options->GetKeepBackupFile())
        revisions->addRevision(GetText());

    // If the user doesn't want to keep the backup files
    // delete it here
    if (!m_options->GetKeepBackupFile() && wxFileExists(filename + ".backup"))
        wxRemoveFile(filename + ".backup");

	// Only mark the editor as saved, if the saving process was successful
	markSaved();
	SetSavePoint();
	UpdateProcedureViewer();

	if (m_fileType == FILE_NSCR || m_fileType == FILE_NPRC)
        SynchronizeBreakpoints();

	m_filetime = fn.GetModificationTime();
	m_bSetUnsaved = false;
	return true;
}


/////////////////////////////////////////////////
/// \brief Saves a NumeRe-specific file and tries
/// to stick to ASCII encoding
///
/// \param filename const wxString&
/// \return bool
///
/////////////////////////////////////////////////
bool NumeReEditor::SaveNumeReFile(const wxString& filename)
{
    // create a std::ofstream to avoid encoding issues
	std::ofstream file;
	file.open(filename.ToStdString().c_str(), std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

	if (!file.is_open())
		return false;

	// write the contents of the file linewise
	for (int i = 0; i < this->GetLineCount(); i++)
	{
		file << this->GetLine(i).ToStdString();
	}

	// flush the files content explicitly
	file.flush();
	file.close();

	return true;
}


/////////////////////////////////////////////////
/// \brief Saves a general file without touching the encoding
///
/// \param filename const wxString&
/// \return bool
///
/////////////////////////////////////////////////
bool NumeReEditor::SaveGeneralFile(const wxString& filename)
{
    // Create file and check, if it has been opened successfully
    wxFile file (filename, wxFile::write);

    if (!file.IsOpened())
    {
        return false;
    }

    // Get text and write it to the file
    wxString buf = GetText();
    bool okay = file.Write(buf.ToStdString().c_str(), buf.ToStdString().length());

    file.Close();

    // Notify caller that there was an error during writing
    if (!okay)
    {
        return false;
    }
    return true;
}


//////////////////////////////////////////////////////////////////////////////
///  public LoadFileText
///  Loads a file from the given string
///
///  @param  fileContents wxString  The text of the file
///
///  @return bool         Whether or not the load succeeded
///
///  @remarks  This isn't actually used right now... probably ought to be cleaned up
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
bool NumeReEditor::LoadFileText(wxString fileContents)
{
	if (fileContents.Length() > 0)
	{
		m_bLoadingFile = true;
		defaultPage = false;
		SetReadOnly(false);
		if (getEditorSetting(SETTING_USETXTADV))
			ToggleSettings(SETTING_USETXTADV);
		ClearAll();
		StyleClearAll();
		InsertText(0, fileContents);
	}

	EmptyUndoBuffer();
	SetSavePoint();
	m_bSetUnsaved = false;

	// determine and set EOL mode
	int eolMode = -1;

	bool eolMix = false;

	wxString eolName;

	if ( fileContents.Contains("\r\n") )
	{
		eolMode = wxSTC_EOL_CRLF;

		eolName = _("CR+LF (Windows)");
	}
	else if ( fileContents.Contains("\r") )
	{
		if (eolMode != -1)
		{
			eolMix = true;
		}
		else
		{
			eolMode = wxSTC_EOL_CR;

			eolName = _("CR (Macintosh)");
		}
	}
	else if ( fileContents.Contains("\n") )
	{
		if (eolMode != -1)
		{
			eolMix = true;
		}
		else
		{
			eolMode = wxSTC_EOL_LF;

			eolName = _("LF (Unix)");
		}
	}

	if ( eolMode != -1 )
	{
		if ( eolMix && wxMessageBox(_("Convert all line endings to ")
									+ eolName + _("?"), _("Line endings"), wxYES_NO | wxICON_QUESTION)
				== wxYES )
		{
			ConvertEOLs(eolMode);

			// set staus bar text
			// g_statustext->Clear();
			//g_statustext->Append(_("Converted line endings to "));
			//g_statustext->Append(eolName);
		}

		SetEOLMode(eolMode);
	}

	m_bLoadingFile = false;
	UpdateSyntaxHighlighting(true);
	return true;
}


//////////////////////////////////////////////////////////////////////////////
///  public Modified
///  Checks whether or not the editor has been modified
///
///  @return bool Whether or not the editor has been modified
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
bool NumeReEditor::Modified ()
{
	// return modified state
	bool modified = GetModify();
	bool readonly = !GetReadOnly();
	bool canundo = CanUndo();



	bool isModified = (modified && readonly && canundo) || m_bSetUnsaved;
	return isModified;
}


//////////////////////////////////////////////////////////////////////////////
///  public OnChar
///  Handles auto-indentation and such whenever the user enters a character
///
///  @param  event wxStyledTextEvent & The generated event
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::OnChar( wxStyledTextEvent& event )
{
	ClearDblClkIndicator();
	//CallAfter(NumeReEditor::AsynchActions);
	const wxChar chr = event.GetKey();
	const int currentLine = GetCurrentLine();
	const int currentPos = GetCurrentPos();
	const int wordstartpos = WordStartPosition(currentPos, true);

	MarkerDeleteAll(MARKER_FOCUSEDLINE);
	if (chr == WXK_TAB)
	{
		event.Skip(true);

		int startLine = LineFromPosition(GetSelectionStart());
		int endline = LineFromPosition(GetSelectionEnd());
		int newStartPos = PositionFromLine(startLine);
		int newEndPos = PositionFromLine(endline) + LineLength(endline);
		bool doIndent = event.GetShift();
		int indentWidth = this->GetIndent();

		this->SetSelection(newStartPos, newEndPos);

		for (int i = startLine; i <= endline; i++)
		{
			int lineIndent = this->GetLineIndentation(i);

			if (doIndent)
			{
				this->SetLineIndentation(i, lineIndent + indentWidth);
			}
			else
			{
				this->SetLineIndentation(i, lineIndent - indentWidth);
			}
		}
	}

	if (chr == '\n')
	{
		markModified(currentLine);
		int previousLineInd = 0;

		if (currentLine > 0)
		{
			markModified(currentLine - 1);
			previousLineInd = GetLineIndentation(currentLine - 1);
		}

		if (previousLineInd == 0)
		{
			return;
		}

		SetLineIndentation(currentLine, previousLineInd);

		// If tabs are being used then change previousLineInd to tab sizes
		if (GetUseTabs())
		{
			previousLineInd /= GetTabWidth();
		}

		GotoPos(PositionFromLine(currentLine) + previousLineInd);
		return;
	}

	if (chr == '"')
	{
		if (GetStyleAt(currentPos) != wxSTC_NSCR_STRING && GetStyleAt(currentPos) != wxSTC_NPRC_STRING)
			InsertText(currentPos, "\"");
	}
	if (chr == '(' || chr == '[' || chr == '{')
	{
		int nMatchingPos = currentPos;
		if (this->HasSelection())
			nMatchingPos = this->GetSelectionEnd();
		if (this->BraceMatch(currentPos - 1) == wxSTC_INVALID_POSITION)
		{
			if (chr == '(')
				InsertText(nMatchingPos, ")");
			else if (chr == '[')
				InsertText(nMatchingPos, "]");
			else
				InsertText(nMatchingPos, "}");
		}
	}

	int lenEntered = currentPos - wordstartpos;

	if (lenEntered > 1
			&& (m_fileType == FILE_NSCR || m_fileType == FILE_NPRC)
			&& GetStyleAt(wordstartpos) != wxSTC_NSCR_COMMENT_LINE
			&& GetStyleAt(wordstartpos) != wxSTC_NSCR_COMMENT_BLOCK
			&& GetStyleAt(wordstartpos) != wxSTC_NSCR_STRING
			&& GetStyleAt(wordstartpos) != wxSTC_NSCR_PROCEDURES
			&& GetStyleAt(wordstartpos) != wxSTC_NPRC_COMMENT_LINE
			&& GetStyleAt(wordstartpos) != wxSTC_NPRC_COMMENT_BLOCK
			&& GetStyleAt(wordstartpos) != wxSTC_NPRC_STRING)
	{
		this->AutoCompSetIgnoreCase(true);
		this->AutoCompSetCaseInsensitiveBehaviour(wxSTC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
		wxString sAutoCompList = generateAutoCompList(GetTextRange(wordstartpos, currentPos), _syntax->getAutoCompList(GetTextRange(wordstartpos, currentPos).ToStdString()));
		if (sAutoCompList.length())
			this->AutoCompShow(lenEntered, sAutoCompList);
	}
	else if (lenEntered > 1
			 && m_fileType == FILE_MATLAB
			 && GetStyleAt(wordstartpos) != wxSTC_MATLAB_COMMENT
			 && GetStyleAt(wordstartpos) != wxSTC_MATLAB_STRING)
	{
		this->AutoCompSetIgnoreCase(true);
		this->AutoCompSetCaseInsensitiveBehaviour(wxSTC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
		wxString sAutoCompList = generateAutoCompList(GetTextRange(wordstartpos, currentPos), _syntax->getAutoCompListMATLAB(GetTextRange(wordstartpos, currentPos).ToStdString()));
		if (sAutoCompList.length())
			this->AutoCompShow(lenEntered, sAutoCompList);
	}
	else if (lenEntered > 1
			 && m_fileType == FILE_CPP
			 && GetStyleAt(wordstartpos) != wxSTC_C_COMMENT
			 && GetStyleAt(wordstartpos) != wxSTC_C_COMMENTLINE
			 && GetStyleAt(wordstartpos) != wxSTC_C_STRING)
	{
		this->AutoCompSetIgnoreCase(true);
		this->AutoCompSetCaseInsensitiveBehaviour(wxSTC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
		wxString sAutoCompList = generateAutoCompList(GetTextRange(wordstartpos, currentPos), _syntax->getAutoCompListCPP(GetTextRange(wordstartpos, currentPos).ToStdString()));
		if (sAutoCompList.length())
			this->AutoCompShow(lenEntered, sAutoCompList);
	}
	else if (lenEntered > 1
			 && (m_fileType == FILE_NSCR || m_fileType == FILE_NPRC)
			 && GetStyleAt(wordstartpos) == wxSTC_NSCR_PROCEDURES)
	{
		wxString sNamespace;
		wxString sSelectedNamespace;
		int nNameSpacePosition = wordstartpos;
		while (GetStyleAt(nNameSpacePosition - 1) == wxSTC_NSCR_PROCEDURES && GetCharAt(nNameSpacePosition - 1) != '$')
			nNameSpacePosition--;
		if (nNameSpacePosition == wordstartpos)
		{
			sNamespace = m_search->FindNameSpaceOfProcedure(wordstartpos) + "~";
		}
		else
			sSelectedNamespace = GetTextRange(nNameSpacePosition, wordstartpos);

		// If namespace == "this~" then replace it with the current namespace
		if (sNamespace == "this~")
		{
			string filename = GetFileNameAndPath().ToStdString();
			filename = replacePathSeparator(filename);
			vector<string> vPaths = m_terminal->getPathSettings();
			if (filename.substr(0, vPaths[PROCPATH].length()) == vPaths[PROCPATH])
			{
				filename.erase(0, vPaths[PROCPATH].length());
				if (filename.find('/') != string::npos)
					filename.erase(filename.rfind('/') + 1);
				while (filename.front() == '/')
					filename.erase(0, 1);
				while (filename.find('/') != string::npos)
					filename[filename.find('/')] = '~';
				sNamespace = filename;
			}
			else
				sNamespace = "";
		}
		else if (sSelectedNamespace == "this~")
		{
			string filename = GetFileNameAndPath().ToStdString();
			filename = replacePathSeparator(filename);
			vector<string> vPaths = m_terminal->getPathSettings();
			if (filename.substr(0, vPaths[PROCPATH].length()) == vPaths[PROCPATH])
			{
				filename.erase(0, vPaths[PROCPATH].length());
				if (filename.find('/') != string::npos)
					filename.erase(filename.rfind('/') + 1);
				while (filename.front() == '/')
					filename.erase(0, 1);
				while (filename.find('/') != string::npos)
					filename[filename.find('/')] = '~';
				sSelectedNamespace = filename;
			}
			else
				sSelectedNamespace = "";
		}
		// If namespace == "thisfile~" then search for all procedures in the current file and use them as the
		// autocompletion list entries
		else if (sNamespace == "thisfile" || sNamespace == "thisfile~" || sSelectedNamespace == "thisfile" || sSelectedNamespace == "thisfile~")
		{
			this->AutoCompSetIgnoreCase(true);
			this->AutoCompSetCaseInsensitiveBehaviour(wxSTC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
			this->AutoCompShow(lenEntered, m_search->FindProceduresInCurrentFile(GetTextRange(wordstartpos, currentPos), sSelectedNamespace));
			this->Colourise(0, -1);
			event.Skip();
			return;
		}
		// If namespace == "main~" (or similiar) then clear it's contents
		else if (sNamespace == "main" || sNamespace == "main~" || sNamespace == "~")
			sNamespace = "";
		else if (sSelectedNamespace == "main" || sSelectedNamespace == "main~" || sSelectedNamespace == "~")
			sSelectedNamespace = "";

		this->AutoCompSetIgnoreCase(true);
		this->AutoCompSetCaseInsensitiveBehaviour(wxSTC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
		this->AutoCompShow(lenEntered, _syntax->getProcAutoCompList(GetTextRange(wordstartpos, currentPos).ToStdString(), sNamespace.ToStdString(), sSelectedNamespace.ToStdString()));
	}
	else if (lenEntered > 1 && m_fileType == FILE_TEXSOURCE && GetStyleAt(wordstartpos) == wxSTC_TEX_COMMAND)
	{
		this->AutoCompSetIgnoreCase(true);
		this->AutoCompSetCaseInsensitiveBehaviour(wxSTC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
		wxString sAutoCompList = generateAutoCompList(GetTextRange(wordstartpos, currentPos), _syntax->getAutoCompListTeX(GetTextRange(wordstartpos, currentPos).ToStdString()));
		if (sAutoCompList.length())
			this->AutoCompShow(lenEntered, sAutoCompList);
	}
	else if (lenEntered > 1
			 && !(m_fileType == FILE_NSCR || m_fileType == FILE_NPRC))
	{
		this->AutoCompSetIgnoreCase(true);
		this->AutoCompSetCaseInsensitiveBehaviour(wxSTC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
		this->AutoCompShow(lenEntered, generateAutoCompList(GetTextRange(wordstartpos, currentPos), ""));
	}
	this->Colourise(0, -1);
	//CallAfter(NumeReEditor::HandleFunctionCallTip);
	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Checks for corresponding braces.
///
/// \return void
///
/// This function checks for corresponding braces and
/// handles the indicators correspondingly.
/////////////////////////////////////////////////
void NumeReEditor::MakeBraceCheck()
{
	char CurrentChar = this->GetCharAt(this->GetCurrentPos());
	char PrevChar = 0;

	// Find the previous character, if available
	if (this->GetCurrentPos())
		PrevChar = this->GetCharAt(this->GetCurrentPos() - 1);

	// Find the matching brace
	if (CurrentChar == ')' || CurrentChar == ']' || CurrentChar == '}')
		getMatchingBrace(this->GetCurrentPos());
	else if (PrevChar == '(' || PrevChar == '[' || PrevChar == '{')
		getMatchingBrace(this->GetCurrentPos() - 1);
	else if (CurrentChar == '(' || CurrentChar == '[' || CurrentChar == '{')
		getMatchingBrace(this->GetCurrentPos());
	else if (PrevChar == ')' || PrevChar == ']' || PrevChar == '}')
		getMatchingBrace(this->GetCurrentPos() - 1);
	else
	{
	    // Deactivate all indicators, if the no brace is at cursor's
	    // position
		this->SetIndicatorCurrent(HIGHLIGHT_MATCHING_BRACE);
		long int maxpos = this->GetLastPosition();
		this->IndicatorClearRange(0, maxpos);
		this->BraceBadLight(wxSTC_INVALID_POSITION);
		this->BraceHighlight(wxSTC_INVALID_POSITION, wxSTC_INVALID_POSITION);
	}

	applyStrikeThrough();
	return;
}


/////////////////////////////////////////////////
/// \brief Checks for corresponding flow control statements.
///
/// \return void
///
/// This function checks for corresponding flow
/// control statements and handles the indicators
/// correspondingly.
/////////////////////////////////////////////////
void NumeReEditor::MakeBlockCheck()
{
	if (this->m_fileType != FILE_NSCR && this->m_fileType != FILE_NPRC && !FILE_MATLAB)
		return;

    // clear all indicators first
	this->SetIndicatorCurrent(HIGHLIGHT_MATCHING_BLOCK);
	this->IndicatorClearRange(0, GetLastPosition());
	this->SetIndicatorCurrent(HIGHLIGHT_NOT_MATCHING_BLOCK);
	this->IndicatorClearRange(0, GetLastPosition());

	// Ensure that we have a command below the cursor
	if (!isStyleType(STYLE_COMMAND, GetCurrentPos()) && !isStyleType(STYLE_COMMAND, GetCurrentPos()-1))
		return;

    // Get the word below the cursor
	wxString currentWord = this->GetTextRange(WordStartPosition(GetCurrentPos(), true), WordEndPosition(GetCurrentPos(), true));

	// If the word is a flow control statement
	// find the matching block
	if (currentWord == "if"
			|| currentWord == "else"
			|| currentWord == "elseif"
			|| currentWord == "endif"
			|| currentWord == "for"
			|| currentWord == "endfor"
			|| currentWord == "while"
			|| currentWord == "endwhile"
			|| currentWord == "compose"
			|| currentWord == "endcompose"
			|| currentWord == "procedure"
			|| currentWord == "endprocedure"
			|| currentWord == "endswitch"
			|| currentWord == "end"
			|| currentWord == "function"
			|| currentWord == "classdef"
			|| currentWord == "properties"
			|| currentWord == "methods"
			|| currentWord == "switch"
			|| currentWord == "case"
			|| currentWord == "otherwise"
			|| currentWord == "default"
			|| currentWord == "try"
			|| currentWord == "catch")
		getMatchingBlock(GetCurrentPos());
}


/////////////////////////////////////////////////
/// \brief This function handles the descriptive
/// function call tip.
///
/// \return void
///
/// The function searches for the corresponding procedure
/// definition or function documentation, formats it
/// correspondingly, searches for the current marked
/// argument and displays the calltip with the current
/// marked argument highlighted. This function is called
/// asynchronously.
/////////////////////////////////////////////////
void NumeReEditor::HandleFunctionCallTip()
{
	// do nothing if an autocompletion list is active
	if (this->AutoCompActive())
		return;

	// do nothing, if language is not supported
	if (this->getFileType() != FILE_NSCR && this->getFileType() != FILE_NPRC)
		return;

	int nStartingBrace = 0;
	int nArgStartPos = 0;
	string sFunctionContext = this->GetCurrentFunctionContext(nStartingBrace);
	string sDefinition;

	if (!sFunctionContext.length())
		return;

	if (sFunctionContext.front() == '$')
	{
		sDefinition = m_search->FindProcedureDefinition().ToStdString();

		if (sDefinition.find('\n') != string::npos)
            sDefinition.erase(sDefinition.find('\n'));

		if (sDefinition.find(')') != string::npos)
			sDefinition.erase(sDefinition.rfind(')') + 1);
	}
	else if (sFunctionContext.front() == '.')
	{
		sDefinition = this->GetMethodCallTip(sFunctionContext.substr(1));
		size_t nDotPos = sDefinition.find('.');

		if (sDefinition.find(')', nDotPos) != string::npos)
			sDefinition.erase(sDefinition.find(')', nDotPos) + 1);
		else
			sDefinition.erase(sDefinition.find(' ', nDotPos));
	}
	else
	{
		sDefinition = this->GetFunctionCallTip(sFunctionContext);

		if (sDefinition.find(')') != string::npos)
			sDefinition.erase(sDefinition.find(')') + 1);
	}

	if (!sDefinition.length())
		return;

	string sArgument = this->GetCurrentArgument(sDefinition, nStartingBrace, nArgStartPos);

	/*if (sArgument.length())
	{
	    if (sArgument.substr(0,3) == "STR")
	        sDefinition += "\n" + _guilang.get("GUI_EDITOR_ARGCALLTIP_STR", sArgument);
	    else if (sArgument == "CHAR")
	        sDefinition += "\n" + _guilang.get("GUI_EDITOR_ARGCALLTIP_CHAR", sArgument);
	    else if (sArgument == "MAT")
	        sDefinition += "\n" + _guilang.get("GUI_EDITOR_ARGCALLTIP_MAT", sArgument);
	    else if (sArgument == "...")
	        sDefinition += "\n" + _guilang.get("GUI_EDITOR_ARGCALLTIP_REPEATTYPE", sArgument);
	    else if (sArgument == "x" || sArgument == "y" || sArgument == "z" || sArgument == "x1" || sArgument == "x0")
	        sDefinition += "\n" + _guilang.get("GUI_EDITOR_ARGCALLTIP_FLOAT", sArgument);
	    else if (sArgument == "l" || sArgument == "n" || sArgument == "m" || sArgument == "k" || sArgument == "P" || sArgument == "POS" || sArgument == "LEN")
	        sDefinition += "\n" + _guilang.get("GUI_EDITOR_ARGCALLTIP_INTEGER", sArgument);
	    else if (sArgument == "theta" || sArgument == "phi")
	        sDefinition += "\n" + _guilang.get("GUI_EDITOR_ARGCALLTIP_ANGLE", sArgument);
	    else
	        sDefinition += "\n" + _guilang.get("GUI_EDITOR_ARGCALLTIP_NOHEURISTICS", sArgument);

	}*/

	if (this->CallTipActive() && this->CallTipStartPos() != nStartingBrace)
	{
		this->AdvCallTipCancel();
		this->AdvCallTipShow(nStartingBrace, sDefinition);
	}
	else if (!this->CallTipActive())
		this->AdvCallTipShow(nStartingBrace, sDefinition);

	if (sArgument.length())
		this->CallTipSetHighlight(nArgStartPos, nArgStartPos + sArgument.length());
}


/////////////////////////////////////////////////
/// \brief Update the assigned procedure viewer.
///
/// \return void
///
/// This member function updates the procedure viewer,
/// if it was registered in this editor
/////////////////////////////////////////////////
void NumeReEditor::UpdateProcedureViewer()
{
    if (m_procedureViewer && m_fileType == FILE_NPRC)
    {
        m_procedureViewer->updateProcedureList(m_search->getProceduresInFile());
    }
}


/////////////////////////////////////////////////
/// \brief Find the function, whose braces the
/// cursor is currently located in.
///
/// \param nStartingBrace int&
/// \return string
///
/////////////////////////////////////////////////
string NumeReEditor::GetCurrentFunctionContext(int& nStartingBrace)
{
	int nCurrentLineStart = this->PositionFromLine(this->GetCurrentLine());
	int nCurrentPos = this->GetCurrentPos();

	// Find the outermost brace for the current function call
	for (int i = nCurrentPos; i > nCurrentLineStart; i--)
	{
		if (this->GetCharAt(i) == '('
				&& (this->BraceMatch(i) >= nCurrentPos || this->BraceMatch(i) == -1) // either no brace (yet) or the brace further right
				&& (this->GetStyleAt(i - 1) == wxSTC_NSCR_FUNCTION || this->GetStyleAt(i - 1) == wxSTC_NSCR_PROCEDURES || this->GetStyleAt(i - 1) == wxSTC_NSCR_METHOD))
		{
			nStartingBrace = i;

			// Get the word in front of the brace and return it
			if (this->GetStyleAt(i - 1) == wxSTC_NSCR_PROCEDURES)
				return m_search->FindMarkedProcedure(i - 1).ToStdString();
			else
			{
				if (this->GetStyleAt(i - 1) == wxSTC_NSCR_METHOD)
					return "." + this->GetTextRange(this->WordStartPosition(i - 1, true), this->WordEndPosition(i - 1, true)).ToStdString();

				return this->GetTextRange(this->WordStartPosition(i - 1, true), this->WordEndPosition(i - 1, true)).ToStdString();
			}
		}
	}

	return "";
}


/////////////////////////////////////////////////
/// \brief Returns the function documentation.
///
/// \param sFunctionName const string&
/// \return string
///
/// The documentation of the current function is
/// obtained from the language files. Functions,
/// which have aliases, are handled as well.
/////////////////////////////////////////////////
string NumeReEditor::GetFunctionCallTip(const string& sFunctionName)
{
	string selection = sFunctionName;

	// Handle aliases
	if (selection == "arcsin")
		selection = "asin";
	else if (selection == "arccos")
		selection = "acos";
	else if (selection == "arctan")
		selection = "atan";
	else if (selection == "arsinh")
		selection = "asinh";
	else if (selection == "arcosh")
		selection = "acosh";
	else if (selection == "artanh")
		selection = "atanh";

	return _guilang.get("PARSERFUNCS_LISTFUNC_FUNC_" + toUpperCase(selection) + "_[*");
}


/////////////////////////////////////////////////
/// \brief Returns the method documentation.
///
/// \param sMethodName const string&
/// \return string
///
/// The documentation of the current method is
/// obtained from the language files.
/////////////////////////////////////////////////
string NumeReEditor::GetMethodCallTip(const string& sMethodName)
{
	if (_guilang.get("PARSERFUNCS_LISTFUNC_METHOD_" + toUpperCase(sMethodName) + "_[STRING]") != "PARSERFUNCS_LISTFUNC_METHOD_" + toUpperCase(sMethodName) + "_[STRING]")
		return "STRINGVAR." + _guilang.get("PARSERFUNCS_LISTFUNC_METHOD_" + toUpperCase(sMethodName) + "_[STRING]");
	else
		return "TABLE()." + _guilang.get("PARSERFUNCS_LISTFUNC_METHOD_" + toUpperCase(sMethodName) + "_[DATA]");
}


/////////////////////////////////////////////////
/// \brief Finds the current argument below the cursor.
///
/// \param sCallTip const string&
/// \param nStartingBrace int
/// \param nArgStartPos int&
/// \return string
///
/// This member function identifies the current
/// active argument of a function or a procedure,
/// on which the cursor is currently located.
/////////////////////////////////////////////////
string NumeReEditor::GetCurrentArgument(const string& sCallTip, int nStartingBrace, int& nArgStartPos)
{
	int nCurrentPos = this->GetCurrentPos();
	int nCurrentArg = 0;
	size_t nParensPos = 0;
	char currentChar;

	// Do nothing, if no parenthesis is found
	if (sCallTip.find('(') == string::npos)
		return "";

	nParensPos = sCallTip.find('(');

	// If this is a method call of a table,
	// advance the position to the method
	// parenthesis, if it is available
	if (sCallTip.find("().") != string::npos)
	{
		if (sCallTip.find('(', sCallTip.find("().") + 3) == string::npos)
			return "";

		nParensPos = sCallTip.find('(', sCallTip.find("().") + 3);
	}

	// Extract the argument list
	string sArgList = sCallTip.substr(nParensPos);
	sArgList.erase(getMatchingParenthesis(sArgList));

	// Find the n-th argument in the editor
	for (int i = nStartingBrace + 1; i < nCurrentPos && i < this->GetLineEndPosition(this->GetCurrentLine()); i++)
	{
	    // Ignore comments and strings
		if (this->GetStyleAt(i) == wxSTC_NSCR_STRING
				|| this->GetStyleAt(i) == wxSTC_NSCR_COMMENT_LINE
				|| this->GetStyleAt(i) == wxSTC_NSCR_COMMENT_BLOCK)
			continue;

		currentChar = this->GetCharAt(i);

		// Increment the argument count, if a comma
		// has been found
		if (currentChar == ',')
			nCurrentArg++;

        // Jump over parentheses and braces
		if ((currentChar == '(' || currentChar == '[' || currentChar == '{')
				&& this->BraceMatch(i) != -1)
			i = this->BraceMatch(i);
	}

	size_t nQuotationMarks = 0;

	// Find the corresponding argument in the argument list
	for (size_t i = 1; i < sArgList.length(); i++)
	{
	    // If this is the current argument and we're after
	    // an opening parenthesis or a comma
		if (!(nQuotationMarks % 2) && !nCurrentArg && (sArgList[i - 1] == '(' || sArgList[i - 1] == ','))
		{
			nArgStartPos = i + nParensPos;
			string sArgument = sArgList.substr(i);

			// If the argument still contains commas,
			// extract them here. Probably it is necessary
			// to move the position to the right
			if (sArgument.find(',') != string::npos)
            {
				sArgument = getNextArgument(sArgument, false);
                nArgStartPos = nParensPos + sArgList.find(sArgument, i);
            }

            // return the extracted argument
			return sArgument;
		}

		// Count and consider quotation marks
		if (sArgList[i] == '"' && sArgList[i-1] != '\\')
            nQuotationMarks++;

		// If a parenthesis or a brace was found,
		// jump over it
		if (!(nQuotationMarks % 2) && (sArgList[i] == '(' || sArgList[i] == '{'))
            i += getMatchingParenthesis(sArgList.substr(i));

		// If a comma was found,
		// decrement the argument count
		if (!(nQuotationMarks % 2) && sArgList[i] == ',')
			nCurrentArg--;
	}

	// Return nothing
	return "";
}


/////////////////////////////////////////////////
/// \brief Returns the starting position of the
/// currently displayed calltip.
///
/// \return int
///
/////////////////////////////////////////////////
int NumeReEditor::CallTipStartPos()
{
	return m_nCallTipStart;
}


/////////////////////////////////////////////////
/// \brief A more advanced calltip display routine.
///
/// \param pos int
/// \param definition const wxString&
/// \return void
///
/// Only updates the calltip, if the contents are
/// different.
/////////////////////////////////////////////////
void NumeReEditor::AdvCallTipShow(int pos, const wxString& definition)
{
	m_nCallTipStart = pos;

	if (m_sCallTipContent != definition)
	{
		if (CallTipActive())
			CallTipCancel();

		m_sCallTipContent = definition;
	}

	CallTipShow(pos, definition);
}


/////////////////////////////////////////////////
/// \brief Simply closes the calltip and resets
/// its associated variables.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::AdvCallTipCancel()
{
	m_nCallTipStart = 0;
	m_sCallTipContent.clear();
	CallTipCancel();
}


/////////////////////////////////////////////////
/// \brief Checks key input events, before they
/// are typed into the editor.
///
/// \param event wxKeyEvent&
/// \return void
///
/// This member function checks the key input events
/// before(!) they are typed into the editor. We catch
/// here parentheses and quotation marks in the case
/// that we have
/// \li either a selection (we then surround the selection)
/// \li or a already matching partner of the parenthesis (we
/// then jump over the matching partner)
/////////////////////////////////////////////////
void NumeReEditor::OnKeyDn(wxKeyEvent& event)
{
    // Check the parentheses in the case of selections
    // and matching partners
	if (this->HasSelection()
			&& event.GetKeyCode() != WXK_SHIFT
			&& event.GetKeyCode() != WXK_CAPITAL
			&& event.GetKeyCode() != WXK_END
			&& event.GetKeyCode() != WXK_HOME)
	{
	    // Selection case: extract the position of the
	    // end of the selection and insert the parenthesis
	    // characters around the selection
		char chr = event.GetKeyCode();
		if (event.ShiftDown() && (chr == '8' || chr == '9'))
		{
			this->BeginUndoAction();
			int selStart = this->GetSelectionStart();
			int selEnd = this->GetSelectionEnd() + 1;
			this->InsertText(selStart, "(");
			this->InsertText(selEnd, ")");
			if (chr == '8')
				this->GotoPos(selStart);
			else
				this->GotoPos(selEnd + 1);
			this->EndUndoAction();
			MakeBraceCheck();
			MakeBlockCheck();
			return;
		}
		else if (event.ShiftDown() && chr == '2')
		{
			this->BeginUndoAction();
			int selStart = this->GetSelectionStart();
			int selEnd = this->GetSelectionEnd() + 1;
			this->InsertText(selStart, "\"");
			this->InsertText(selEnd, "\"");
			this->GotoPos(selEnd + 1);
			this->EndUndoAction();
			MakeBraceCheck();
			MakeBlockCheck();
			return;
		}
		else if (event.ControlDown() && event.AltDown() && (chr == '8' || chr == '9')) // Alt Gr means CTRL+ALT
		{
			this->BeginUndoAction();
			int selStart = this->GetSelectionStart();
			int selEnd = this->GetSelectionEnd() + 1;
			this->InsertText(selStart, "[");
			this->InsertText(selEnd, "]");
			if (chr == '8')
				this->GotoPos(selStart);
			else
				this->GotoPos(selEnd + 1);
			this->EndUndoAction();
			MakeBraceCheck();
			MakeBlockCheck();
			return;
		}
		else if (event.ControlDown() && event.AltDown() && (chr == '7' || chr == '0'))
		{
			this->BeginUndoAction();
			int selStart = this->GetSelectionStart();
			int selEnd = this->GetSelectionEnd() + 1;
			this->InsertText(selStart, "{");
			this->InsertText(selEnd, "}");
			if (chr == '7')
				this->GotoPos(selStart);
			else
				this->GotoPos(selEnd + 1);
			this->EndUndoAction();
			MakeBraceCheck();
			MakeBlockCheck();
			return;
		}
	}
	else if (event.GetKeyCode() != WXK_SHIFT
			&& event.GetKeyCode() != WXK_CAPITAL
			&& event.GetKeyCode() != WXK_END
			&& event.GetKeyCode() != WXK_HOME)
    {
        // Matching partner case: if the matching partner
        // is right to the current input position, simply
        // jump one position to the right. Note that this
        // algorithm will not work in strings, because
        // parenthesis matching in strings is not necessary
        char chr = event.GetKeyCode();
		if (event.ShiftDown() && chr == '9')
		{
		    if (!isStyleType(STYLE_STRING, GetCurrentPos()) && GetCharAt(GetCurrentPos()) == ')' && BraceMatch(GetCurrentPos()) != wxSTC_INVALID_POSITION)
            {
                GotoPos(GetCurrentPos()+1);
                return;
            }
		}
		else if (event.ShiftDown() && chr == '2')
		{
		    if (isStyleType(STYLE_STRING, GetCurrentPos()-1) && GetCharAt(GetCurrentPos()) == '"' && GetCharAt(GetCurrentPos()-1) != '\\')
            {
                GotoPos(GetCurrentPos()+1);
                return;
            }
		}
		else if (event.ControlDown() && event.AltDown() && chr == '9') // Alt Gr means CTRL+ALT
		{
			if (!isStyleType(STYLE_STRING, GetCurrentPos()) && GetCharAt(GetCurrentPos()) == ']' && BraceMatch(GetCurrentPos()) != wxSTC_INVALID_POSITION)
            {
                GotoPos(GetCurrentPos()+1);
                return;
            }
		}
		else if (event.ControlDown() && event.AltDown() && chr == '0')
		{
			if (!isStyleType(STYLE_STRING, GetCurrentPos()) && GetCharAt(GetCurrentPos()) == '}' && BraceMatch(GetCurrentPos()) != wxSTC_INVALID_POSITION)
            {
                GotoPos(GetCurrentPos()+1);
                return;
            }
		}
    }

	// Pass the control to the internal OnKeyDown event
	// handler, which will insert the correct character
	OnKeyDown(event);

	// Apply the brace match indicator, if we have only
	// one or no selection
	if (this->GetSelections() <= 1)
		MakeBraceCheck();

	// Apply the block match indicator
	MakeBlockCheck();

	// Clear the double click occurence highlighter,
	// if the user doesn't press Ctrl or Shift.
	if (!event.ControlDown() && !event.ShiftDown())
		ClearDblClkIndicator();
}


/////////////////////////////////////////////////
/// \brief Called, when the user releases a key.
///
/// \param event wxKeyEvent&
/// \return void
///
/// Performs highlighting and asynchronous actions,
/// which are time-consuming tasks.
/////////////////////////////////////////////////
void NumeReEditor::OnKeyRel(wxKeyEvent& event)
{
	if (this->GetSelections() <= 1)
		MakeBraceCheck();

	MakeBlockCheck();
	event.Skip();
	CallAfter(NumeReEditor::AsynchActions);
}


/////////////////////////////////////////////////
/// \brief Called, when the user releases the mouse key.
///
/// \param event wxMouseEvent&
/// \return void
///
/// Performs highlighting and asynchronous actions,
/// which are time-consuming tasks.
/////////////////////////////////////////////////
void NumeReEditor::OnMouseUp(wxMouseEvent& event)
{
	MakeBraceCheck();
	MakeBlockCheck();
	CallAfter(NumeReEditor::AsynchActions);
	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Called, when the user presses the left
/// mouse key.
///
/// \param event wxMouseEvent&
/// \return void
///
/// Performs highlighting and removes the double-
/// click indicator.
/////////////////////////////////////////////////
void NumeReEditor::OnMouseDn(wxMouseEvent& event)
{
	if (!event.ControlDown())
		ClearDblClkIndicator();

	MakeBraceCheck();
	MakeBlockCheck();
	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Called, when the user double clicks.
///
/// \param event wxMouseEvent&
/// \return void
///
/// Searches automatically for all occurences of
/// a string in the document and highlightes them
/////////////////////////////////////////////////
void NumeReEditor::OnMouseDblClk(wxMouseEvent& event)
{
	int charpos = PositionFromPoint(event.GetPosition());
	int startPosition = WordStartPosition(charpos, true);
	int endPosition = WordEndPosition(charpos, true);
	wxString selection = this->GetTextRange(startPosition, endPosition);

	// Ensure the user has selected a word
	if (!selection.length())
	{
		event.Skip();
		return;
	}

	// Handle the control key and add the current selection
	// to the already existing one
	if (event.ControlDown() && this->HasSelection())
		this->AddSelection(endPosition, startPosition);
	else
		this->SetSelection(startPosition, endPosition);

	m_dblclkString = selection;
	long int maxpos = GetLastPosition();

	// Update the Indicators
	SetIndicatorCurrent(HIGHLIGHT_DBLCLK);
	IndicatorClearRange(0, maxpos);
	IndicatorSetStyle(HIGHLIGHT_DBLCLK, wxSTC_INDIC_ROUNDBOX);
	IndicatorSetAlpha(HIGHLIGHT_DBLCLK, 80);
	IndicatorSetForeground(HIGHLIGHT_DBLCLK, wxColor(0, 255, 0));

	int nPos = 0;
	int nCurr = 0;
	int nLength = endPosition - startPosition;
	vector<int> vSelectionList;

	// Find all occurences and store them in a vector
	while ((nPos = FindText(nCurr, maxpos, selection, wxSTC_FIND_MATCHCASE | wxSTC_FIND_WHOLEWORD)) != wxNOT_FOUND)
	{
		vSelectionList.push_back(nPos);
		nCurr = nPos + nLength;
	}

	// Highlight the occurences
	for (size_t i = 0; i < vSelectionList.size(); i++)
		this->IndicatorFillRange(vSelectionList[i], nLength);

	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Called, when the mouse leaves the editor
/// screen, but the user keeps the mouse pressed.
///
/// \param event wxMouseCaptureLostEvent&
/// \return void
///
/// The capturing of the mouse is ended and the
/// editor window is refreshed
/////////////////////////////////////////////////
void NumeReEditor::OnMouseCaptureLost(wxMouseCaptureLostEvent& event)
{
	if (GetCapture() == this)
	{
		ReleaseMouse();
		Refresh();
	}
}


/////////////////////////////////////////////////
/// \brief Called, when the mouse enters the editor window.
///
/// \param event wxMouseEvent&
/// \return void
///
/// Focuses the editor, so that the user can start
/// typing directly.
/////////////////////////////////////////////////
void NumeReEditor::OnEnter(wxMouseEvent& event)
{
	if (g_findReplace != nullptr && g_findReplace->IsShown())
	{
		event.Skip();
		return;
	}

	this->SetFocus();
	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Called, when the mouse leaves the editor window.
///
/// \param event wxMouseEvent&
/// \return void
///
/// Cancels all open call tips, if there are any.
/////////////////////////////////////////////////
void NumeReEditor::OnLeave(wxMouseEvent& event)
{
	if (this->CallTipActive())
		this->AdvCallTipCancel();

	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Called, when the editor loses focus.
///
/// \param event wxFocusEvent&
/// \return void
///
/// Cancels all open call tips, if there are any.
/////////////////////////////////////////////////
void NumeReEditor::OnLoseFocus(wxFocusEvent& event)
{
	if (this->CallTipActive())
		this->AdvCallTipCancel();

	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Called, when the mouse dwells for some time.
///
/// \param event wxStyledTextEvent&
/// \return void
///
/// This function displays the correct call tip
/// depending upon the syntax element below the
/// mouse pointer.
///
/// \todo HAS TO BE REFACTORED
/////////////////////////////////////////////////
void NumeReEditor::OnMouseDwell(wxStyledTextEvent& event)
{
	if ((m_fileType != FILE_NSCR && m_fileType != FILE_NPRC) || m_PopUpActive || !this->HasFocus())
		return;

	int charpos = event.GetPosition();
	int startPosition = WordStartPosition(charpos, true);
	int endPosition = WordEndPosition(charpos, true);

	wxString selection = this->GetTextRange(startPosition, endPosition);

	if (GetStyleAt(charpos) == wxSTC_NSCR_FUNCTION)
	{
		if (this->CallTipActive() && m_nCallTipStart == startPosition)
            return;
        else
            this->AdvCallTipCancel();

		size_t lastpos = 22;
		this->AdvCallTipShow(startPosition, addLinebreaks(realignLangString(GetFunctionCallTip(selection.ToStdString()), lastpos)));
		this->CallTipSetHighlight(0, lastpos);
	}
	else if (GetStyleAt(charpos) == wxSTC_NSCR_COMMAND || this->GetStyleAt(charpos) == wxSTC_NSCR_PROCEDURE_COMMANDS)
	{
		if (this->CallTipActive() && m_nCallTipStart == startPosition)
            return;
        else
            this->AdvCallTipCancel();

		if (selection == "showf")
			selection = "show";
		else if (selection == "view")
			selection = "edit";
		else if (selection == "undef")
			selection = "undefine";
		else if (selection == "ifndef")
			selection = "ifndefined";
		else if (selection == "redef")
			selection = "redefine";
		else if (selection == "del")
			selection = "delete";
		else if (selection == "search")
			selection = "find";
		else if (selection == "vector")
			selection = "vect";
		else if (selection == "vector3d")
			selection = "vect3d";
		else if (selection == "graph")
			selection = "plot";
		else if (selection == "graph3d")
			selection = "plot3d";
		else if (selection == "gradient")
			selection = "grad";
		else if (selection == "gradient3d")
			selection = "grad3d";
		else if (selection == "surface")
			selection = "surf";
		else if (selection == "surface3d")
			selection = "surf3d";
		else if (selection == "meshgrid")
			selection = "mesh";
		else if (selection == "meshgrid3d")
			selection = "mesh3d";
		else if (selection == "density")
			selection = "dens";
		else if (selection == "density3d")
			selection = "dens3d";
		else if (selection == "contour")
			selection = "cont";
		else if (selection == "contour3d")
			selection = "cont3d";
		else if (selection == "mtrxop")
			selection = "matop";
		else if (selection == "man")
			selection = "help";
		else if (selection == "credits" || selection == "info")
			selection = "about";
		else if (selection == "integrate2" || selection == "integrate2d")
			selection = "integrate";

		size_t lastpos = 0;

		if (selection == "if" || selection == "endif" || selection == "else" || selection == "elseif")
		{
			size_t nLength = 0;
			size_t lastpos2 = 0;
			string sBlock = addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_IF_*"), lastpos)) + "\n  [...]\n";

			if (selection != "if")
				nLength = sBlock.length();

			sBlock += addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_ELSEIF_*"), lastpos2)) + "\n  [...]\n";

			if (selection != "if" && selection != "elseif")
				nLength = sBlock.length() + countUmlauts(sBlock);

			sBlock += addLinebreaks(_guilang.get("PARSERFUNCS_LISTCMD_CMD_ELSE_*")) + "\n  [...]\n";

			if (selection != "if" && selection != "elseif" && selection != "else")
				nLength = sBlock.length() + countUmlauts(sBlock);

			sBlock += addLinebreaks(_guilang.get("PARSERFUNCS_LISTCMD_CMD_ENDIF_*"));
			this->AdvCallTipShow(startPosition, sBlock);

			if (selection == "if")
				this->CallTipSetHighlight(nLength, lastpos + nLength);
			else if (selection == "elseif")
				this->CallTipSetHighlight(nLength, lastpos2 + nLength);
			else
				this->CallTipSetHighlight(nLength, 13 + nLength);
		}
		else if (selection == "switch" || selection == "endswitch" || selection == "case" || selection == "default")
		{
			size_t nLength = 0;
			size_t lastpos2 = 0;
			string sBlock = addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_SWITCH_*"), lastpos)) + "\n  [...]\n";

			if (selection != "switch")
				nLength = sBlock.length();

			sBlock += addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_CASE_*"), lastpos2)) + "\n  [...]\n";

			if (selection != "switch" && selection != "case")
				nLength = sBlock.length() + countUmlauts(sBlock);

			sBlock += addLinebreaks(_guilang.get("PARSERFUNCS_LISTCMD_CMD_DEFAULT_*")) + "\n  [...]\n";

			if (selection != "switch" && selection != "case" && selection != "default")
				nLength = sBlock.length() + countUmlauts(sBlock);

			sBlock += addLinebreaks(_guilang.get("PARSERFUNCS_LISTCMD_CMD_ENDSWITCH_*"));
			this->AdvCallTipShow(startPosition, sBlock);

			if (selection == "switch")
				this->CallTipSetHighlight(nLength, lastpos + nLength);
			else if (selection == "case")
				this->CallTipSetHighlight(nLength, lastpos2 + nLength);
			else
				this->CallTipSetHighlight(nLength, 13 + nLength);
		}
		else if (selection == "for" || selection == "endfor")
		{
			size_t nLength = 0;
			size_t lastpos2 = 0;
			string sBlock = addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_FOR_*"), lastpos)) + "\n  [...]\n";

			if (selection != "for")
				nLength = sBlock.length() + countUmlauts(sBlock);

			sBlock += addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_ENDFOR_*"), lastpos2));
			this->AdvCallTipShow(startPosition, sBlock);

			if (nLength)
				this->CallTipSetHighlight(nLength, lastpos2 + nLength);
			else
				this->CallTipSetHighlight(nLength, lastpos + nLength);
		}
		else if (selection == "while" || selection == "endwhile")
		{
			size_t nLength = 0;
			size_t lastpos2 = 0;
			string sBlock = addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_WHILE_*"), lastpos)) + "\n  [...]\n";

			if (selection != "while")
				nLength = sBlock.length() + countUmlauts(sBlock);

			sBlock += addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_ENDWHILE_*"), lastpos2));
			this->AdvCallTipShow(startPosition, sBlock);

			if (nLength)
				this->CallTipSetHighlight(nLength, lastpos2 + nLength);
			else
				this->CallTipSetHighlight(nLength, lastpos + nLength);
		}
		else if (selection == "procedure" || selection == "endprocedure")
		{
			size_t nLength = 0;
			string sBlock = addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_PROCEDURE_*"), lastpos)) + "\n  [...]\n";

			if (selection != "procedure")
				nLength = sBlock.length() + countUmlauts(sBlock);

			sBlock += addLinebreaks(_guilang.get("PARSERFUNCS_LISTCMD_CMD_ENDPROCEDURE_*"));
			this->AdvCallTipShow(startPosition, sBlock);

			if (nLength)
				this->CallTipSetHighlight(nLength, 13 + nLength);
			else
				this->CallTipSetHighlight(nLength, lastpos + nLength);
		}
		else if (selection == "compose" || selection == "endcompose")
		{
			size_t nLength = 0;
			string sBlock = addLinebreaks(_guilang.get("PARSERFUNCS_LISTCMD_CMD_COMPOSE_*")) + "\n  [...]\n";

			if (selection != "compose")
				nLength = sBlock.length() + countUmlauts(sBlock);

			sBlock += addLinebreaks(_guilang.get("PARSERFUNCS_LISTCMD_CMD_ENDCOMPOSE_*"));
			this->AdvCallTipShow(startPosition, sBlock);
			this->CallTipSetHighlight(nLength, 13 + nLength);
		}
		else
		{
			this->AdvCallTipShow(startPosition, addLinebreaks(realignLangString(_guilang.get("PARSERFUNCS_LISTCMD_CMD_" + toUpperCase(selection.ToStdString()) + "_*"), lastpos)));
			this->CallTipSetHighlight(0, lastpos);
		}
	}
	else if (GetStyleAt(charpos) == wxSTC_NSCR_PROCEDURES)
	{
		if (GetCharAt(charpos) != '$')
            startPosition--;

        if (this->CallTipActive() && m_nCallTipStart == startPosition)
            return;
        else
            this->AdvCallTipCancel();

        wxString proc = m_search->FindMarkedProcedure(charpos);

		if (!proc.length())
			return;

		wxString procdef = m_search->FindProcedureDefinition();
		wxString flags = "";

		if (!procdef.length())
			procdef = m_clickedProcedure + "(...)";

		if (procdef.find("::") != string::npos)
		{
			flags = procdef.substr(procdef.find("::"));
			procdef.erase(procdef.find("::"));
		}
		else if (procdef.find('\n') != string::npos)
        {
            flags = procdef.substr(procdef.find('\n'));
            procdef.erase(procdef.find('\n'));
        }

		if (flags.find('\n') != string::npos)
            this->AdvCallTipShow(startPosition, procdef + flags);
        else
            this->AdvCallTipShow(startPosition, procdef + flags + "\n    " + _guilang.get("GUI_EDITOR_CALLTIP_PROC2"));

		this->CallTipSetHighlight(0, procdef.length());
	}
	else if (this->GetStyleAt(charpos) == wxSTC_NSCR_OPTION)
	{
		if (this->CallTipActive() && m_nCallTipStart == startPosition)
            return;
        else
            this->AdvCallTipCancel();

		selection = _guilang.get("GUI_EDITOR_CALLTIP_OPT_" + toUpperCase(selection.ToStdString()));
		size_t highlightlength = selection.length();

		if (selection.find(' ') != string::npos)
			highlightlength = selection.find(' ');

		this->AdvCallTipShow(startPosition, "Option: " + selection);
		this->CallTipSetHighlight(8, 8 + highlightlength);
	}
	else if (this->GetStyleAt(charpos) == wxSTC_NSCR_METHOD)
	{
		if (this->CallTipActive() && m_nCallTipStart == startPosition)
            return;
        else
            this->AdvCallTipCancel();

		selection = GetMethodCallTip(selection.ToStdString());
		size_t highlightlength;
		size_t highlightStart = selection.find('.') + 1;

		if (selection.find(' ') != string::npos)
			highlightlength = selection.find(' ');

		this->AdvCallTipShow(startPosition, addLinebreaks(realignLangString(selection.ToStdString(), highlightlength)));
		this->CallTipSetHighlight(highlightStart, highlightlength);
	}
	else if (this->GetStyleAt(charpos) == wxSTC_NSCR_PREDEFS)
	{
		if (this->CallTipActive() && m_nCallTipStart == startPosition)
            return;
        else
            this->AdvCallTipCancel();

		size_t highlightLength = 10;
		this->AdvCallTipShow(startPosition, addLinebreaks(realignLangString(_guilang.get("GUI_EDITOR_CALLTIP_" + toUpperCase(selection.ToStdString())), highlightLength)));
		this->CallTipSetHighlight(0, highlightLength);
	}
	else if (this->GetStyleAt(charpos) == wxSTC_NSCR_CONSTANTS)
	{
		if (this->CallTipActive() && m_nCallTipStart == startPosition)
            return;
        else
            this->AdvCallTipCancel();

		string sCalltip = _guilang.get("GUI_EDITOR_CALLTIP_CONST" + toUpperCase(selection.ToStdString()) + "_*");

		if (selection == "_G")
			sCalltip = _guilang.get("GUI_EDITOR_CALLTIP_CONST_GRAV_*");

		this->AdvCallTipShow(startPosition, sCalltip);
		this->CallTipSetHighlight(0, sCalltip.find('='));
	}
}


/////////////////////////////////////////////////
/// \brief Called, when the editor idles, i.e. the
/// user is not using it.
///
/// \param event wxIdleEvent&
/// \return void
///
/// Calls the time-consuming asynchronous evaluation,
/// if the editor contents had been modified before.
/////////////////////////////////////////////////
void NumeReEditor::OnIdle(wxIdleEvent& event)
{
	if (!m_modificationHappened)
		return;

	m_modificationHappened = false;
	CallAfter(NumeReEditor::AsynchEvaluations);
}


/////////////////////////////////////////////////
/// \brief Called, when the editor reaches the
/// latest save point.
///
/// \param event wxStyledTextEvent&
/// \return void
///
/// The save point is reached by using undo and redo
/// actions. The editor will mark all remaining modifications
/// as saved.
/////////////////////////////////////////////////
void NumeReEditor::OnSavePointReached(wxStyledTextEvent& event)
{
	markSaved();
	m_bSetUnsaved = false;
	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Called, when the editor leaves the
/// latest save point.
///
/// \param event wxStyledTextEvent&
/// \return void
///
/// The save point is left by undo and redo actions
/// and actual modifications. The editor will be
/// marked as containing unsaved parts.
/////////////////////////////////////////////////
void NumeReEditor::OnSavePointLeft(wxStyledTextEvent& event)
{
	SetUnsaved();
}


/////////////////////////////////////////////////
/// \brief Toggles a line comment.
///
/// \return void
///
/// This function comments lines, which are not
/// commented and uncomments lines, which are
/// commented.
///
/// \todo HAS TO BE REFACTORED
/////////////////////////////////////////////////
void NumeReEditor::ToggleCommentLine()
{
	if (m_fileType == FILE_NONSOURCE)
		return;

	int nFirstLine = 0;
	int nLastLine = 0;
	int nSelectionStart = -1;
	int nSelectionEnd = 0;

	if (HasSelection())
	{
		nSelectionStart = GetSelectionStart();
		nSelectionEnd = GetSelectionEnd();
		nFirstLine = LineFromPosition(nSelectionStart);
		nLastLine = LineFromPosition(nSelectionEnd);
	}
	else
	{
		nFirstLine = GetCurrentLine();
		nLastLine = nFirstLine;
	}

	BeginUndoAction();

	for (int i = nFirstLine; i <= nLastLine; i++)
	{
		int position = PositionFromLine(i);

		while (GetCharAt(position) == ' ' || GetCharAt(position) == '\t')
			position++;

		int style = GetStyleAt(position);

		if ((m_fileType == FILE_NSCR || m_fileType == FILE_NPRC)
				&& (style == wxSTC_NPRC_COMMENT_LINE || style == wxSTC_NSCR_COMMENT_LINE))
		{
			if (this->GetCharAt(position + 2) == ' ')
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 3)
					nSelectionStart -= 3;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 3);
				nSelectionEnd -= 3;
			}
			else
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 2)
					nSelectionStart -= 2;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 2);
				nSelectionEnd -= 2;
			}
		}
		else if ((m_fileType == FILE_NSCR || m_fileType == FILE_NPRC)
				 && !(style == wxSTC_NPRC_COMMENT_LINE || style == wxSTC_NSCR_COMMENT_LINE))
		{
			this->InsertText(this->PositionFromLine(i), "## " );

			if (nSelectionStart >= 0)
			{
				nSelectionStart += 3;
				nSelectionEnd += 3;
			}
		}
		else if (m_fileType == FILE_TEXSOURCE && GetStyleAt(position + 1) == wxSTC_TEX_DEFAULT && GetCharAt(position) == '%')
		{
			if (this->GetCharAt(position + 1) == ' ')
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 2)
					nSelectionStart -= 2;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 2);
				nSelectionEnd -= 2;
			}
			else
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 1)
					nSelectionStart -= 1;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 1);
				nSelectionEnd -= 1;
			}
		}
		else if (m_fileType == FILE_TEXSOURCE && GetStyleAt(position + 1) != wxSTC_TEX_DEFAULT && GetCharAt(position) != '%')
		{
			this->InsertText(this->PositionFromLine(i), "% " );

			if (nSelectionStart >= 0)
			{
				nSelectionStart += 2;
				nSelectionEnd += 2;
			}
		}
		else if (m_fileType == FILE_DATAFILES && style == wxSTC_MATLAB_COMMENT)
		{
			if (this->GetCharAt(position + 1) == ' ')
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 2)
					nSelectionStart -= 2;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 2);
				nSelectionEnd -= 2;
			}
			else
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 1)
					nSelectionStart -= 1;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 1);
				nSelectionEnd -= 1;
			}
		}
		else if (m_fileType == FILE_DATAFILES && style != wxSTC_MATLAB_COMMENT)
		{
			this->InsertText(this->PositionFromLine(i), "# " );

			if (nSelectionStart >= 0)
			{
				nSelectionStart += 2;
				nSelectionEnd += 2;
			}
		}
		else if (m_fileType == FILE_MATLAB && style == wxSTC_MATLAB_COMMENT)
		{
			if (this->GetCharAt(position + 1) == ' ')
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 2)
					nSelectionStart -= 2;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 2);
				nSelectionEnd -= 2;
			}
			else
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 1)
					nSelectionStart -= 1;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 1);
				nSelectionEnd -= 1;
			}
		}
		else if (m_fileType == FILE_MATLAB && style != wxSTC_MATLAB_COMMENT)
		{
			this->InsertText(this->PositionFromLine(i), "% " );

			if (nSelectionStart >= 0)
			{
				nSelectionStart += 2;
				nSelectionEnd += 2;
			}
		}
		else if (m_fileType == FILE_CPP && style == wxSTC_C_COMMENTLINE)
		{
			if (this->GetCharAt(position + 1) == ' ')
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 2)
					nSelectionStart -= 2;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 3);
				nSelectionEnd -= 2;
			}
			else
			{
				if (i == nFirstLine && nSelectionStart >= 0 && nSelectionStart >= position + 1)
					nSelectionStart -= 1;
				else if (i == nFirstLine && nSelectionStart >= 0)
					nSelectionStart = position;

				this->DeleteRange(position, 2);
				nSelectionEnd -= 1;
			}
		}
		else if (m_fileType == FILE_CPP && style != wxSTC_C_COMMENTLINE)
		{
			this->InsertText(this->PositionFromLine(i), "// " );

			if (nSelectionStart >= 0)
			{
				nSelectionStart += 3;
				nSelectionEnd += 3;
			}
		}
	}

	if (nSelectionStart >= 0)
		SetSelection(nSelectionStart, nSelectionEnd);

	EndUndoAction();
}


/////////////////////////////////////////////////
/// \brief Toggles block comments in a selection.
///
/// \return void
///
/// This function comments selections, which are
/// not commented and uncomments selections, which
/// are commented. This function uses the block
/// comment style for this feature.
///
/// \todo HAS TO BE REFACTORED
/////////////////////////////////////////////////
void NumeReEditor::ToggleCommentSelection()
{
	if (m_fileType == FILE_NONSOURCE)
		return;

	if (!HasSelection())
		return;

	int nFirstPosition = this->GetSelectionStart();
	int nLastPosition = this->GetSelectionEnd();
	int nSelectionStart = nFirstPosition;
	int nSelectionEnd = nLastPosition;
	int style = GetStyleAt(nFirstPosition);

	if (m_fileType != FILE_NSCR && m_fileType != FILE_NPRC)
	{
		ToggleCommentLine();
		return;
	}

	BeginUndoAction();

	if (style == wxSTC_NPRC_COMMENT_BLOCK || style == wxSTC_NSCR_COMMENT_BLOCK)
	{
		// Position before
		while (nFirstPosition && (GetStyleAt(nFirstPosition - 1) == wxSTC_NPRC_COMMENT_BLOCK || GetStyleAt(nFirstPosition - 1) == wxSTC_NSCR_COMMENT_BLOCK))
			nFirstPosition--;

		if (GetStyleAt(nLastPosition) != wxSTC_NPRC_COMMENT_BLOCK || GetStyleAt(nLastPosition) != wxSTC_NSCR_COMMENT_BLOCK)
			nLastPosition = nFirstPosition;

		// Position after
		while (nLastPosition < this->GetLastPosition() && (GetStyleAt(nLastPosition) == wxSTC_NPRC_COMMENT_BLOCK || GetStyleAt(nLastPosition) == wxSTC_NSCR_COMMENT_BLOCK))
			nLastPosition++;

		if (this->GetTextRange(nLastPosition - 3, nLastPosition) == " *#")
		{
			if (nSelectionEnd > nLastPosition - 3)
				nSelectionEnd -= 3;

			this->DeleteRange(nLastPosition - 3, 3);
		}
		else
		{
			if (nSelectionEnd > nLastPosition - 2)
				nSelectionEnd -= 2;

			this->DeleteRange(nFirstPosition - 2, 2);
		}

		if (this->GetTextRange(nFirstPosition, nFirstPosition + 3) == "#* ")
		{
			if (nFirstPosition != nSelectionStart)
				nSelectionStart -= 3;

			this->DeleteRange(nFirstPosition, 3);
			nSelectionEnd -= 3;
		}
		else
		{
			if (nFirstPosition != nSelectionStart)
				nSelectionStart -= 2;

			this->DeleteRange(nFirstPosition, 2);
			nSelectionEnd -= 2;
		}
	}
	else if (!(style == wxSTC_NPRC_COMMENT_LINE || style == wxSTC_NSCR_COMMENT_LINE))
	{
		this->InsertText(nFirstPosition, "#* ");
		this->InsertText(nLastPosition + 3, " *#");
		nSelectionEnd += 3;
		nSelectionStart += 3;
	}

	EndUndoAction();
	SetSelection(nSelectionStart, nSelectionEnd);
}


/////////////////////////////////////////////////
/// \brief Folds the code completely.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::FoldAll()
{
	for (int i = GetLineCount() - 1; i >= 0; i--)
	{
		if (GetFoldLevel(i) & wxSTC_FOLDLEVELHEADERFLAG && GetFoldExpanded(i))
			ToggleFold(i);
	}
}


/////////////////////////////////////////////////
/// \brief Unfolds every folded section.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::UnfoldAll()
{
	for (int i = 0; i < GetLineCount(); i++)
	{
		if (GetFoldLevel(i) & wxSTC_FOLDLEVELHEADERFLAG && !GetFoldExpanded(i))
			ToggleFold(i);
	}
}


/////////////////////////////////////////////////
/// \brief Jumps the cursor to the next bookmark.
///
/// \param down bool
/// \return void
///
/// This function uses bookmark markers and section
/// markers as targets for its jumps.
/////////////////////////////////////////////////
void NumeReEditor::JumpToBookmark(bool down)
{
	int nCurrentLine = GetCurrentLine();

	// Leave the line, if it contains already one of both markers
	if (MarkerOnLine(nCurrentLine, MARKER_BOOKMARK) || MarkerOnLine(nCurrentLine, MARKER_SECTION))
	{
		if (down)
			nCurrentLine++;
		else
			nCurrentLine--;
	}

	int nMarker = nCurrentLine;
	int nMarkerMask = (1 << MARKER_BOOKMARK) | (1 << MARKER_SECTION);

	// Find the next marker
	if (down)
		nMarker = MarkerNext(nCurrentLine, nMarkerMask);
	else
		nMarker = MarkerPrevious(nCurrentLine, nMarkerMask);

	// Wrap around the search, if nothing was found
	if (nMarker == -1)
	{
		if (down)
			nMarker = MarkerNext(0, nMarkerMask);
		else
			nMarker = MarkerPrevious(LineFromPosition(GetLastPosition()), nMarkerMask);
	}

	// Go to the marker, if a marker was found
	if (nMarker != -1)
		GotoLine(nMarker);
}


/////////////////////////////////////////////////
/// \brief Returns the line positions of the bookmarks.
///
/// \return vector<int>
///
/// This member function returns a vector containing the list
/// of available bookmarks in the current file.
/////////////////////////////////////////////////
vector<int> NumeReEditor::getBookmarks()
{
    vector<int> vBookmarks;

    // Find all bookmark markers in the document
    for (int i = 0; i < GetLineCount(); i++)
    {
        if (MarkerOnLine(i, MARKER_BOOKMARK))
            vBookmarks.push_back(i);
    }

    return vBookmarks;
}


/////////////////////////////////////////////////
/// \brief Set the bookmarks in the editor.
///
/// \param vBookmarks const vector<int>&
/// \return void
///
/// This member function overrides all bookmarks in the current
/// file with the passed list of bookmarks.
/////////////////////////////////////////////////
void NumeReEditor::setBookmarks(const vector<int>& vBookmarks)
{
    // Remove all available bookmark markers
    MarkerDeleteAll(MARKER_BOOKMARK);

    // Set the list of bookmarks
    for (size_t i = 0; i < vBookmarks.size(); i++)
    {
        MarkerAdd(vBookmarks[i], MARKER_BOOKMARK);
    }
}


/////////////////////////////////////////////////
/// \brief Removes whitespaces in the document.
///
/// \param nType int Type of removal
/// \return void
///
/// The type of removal is selected using nType.
/// RM_WS_FRONT removes leading, RM_WS_BACK removes
/// trailing and RM_WS_BOTH removes leading and
/// trailing whitespaces from each line.
/////////////////////////////////////////////////
void NumeReEditor::removeWhiteSpaces(int nType)
{
	int nFirstline = 0;
	int nLastLine = GetLineCount() - 1;

	// If the user selected something, use the
	// selection as begin and end lines
	if (HasSelection())
	{
		nFirstline = LineFromPosition(GetSelectionStart());
		nLastLine = LineFromPosition(GetSelectionEnd());
	}

	BeginUndoAction();

	// Go through each line and remove the white
	// spaces depending on the selected type
	for (int i = nFirstline; i <= nLastLine; i++)
	{
		if (nType == RM_WS_FRONT)
			SetLineIndentation(i, 0);
		else if (nType == RM_WS_BACK || nType == RM_WS_BOTH)
		{
			wxString sLine = this->GetLine(i);

			if (sLine.find_first_of("\r\n") != string::npos)
				sLine.erase(sLine.find_first_of("\r\n"));

			int nLineEndPos = sLine.length();

			while (nLineEndPos && (sLine[nLineEndPos - 1] == ' ' || sLine[nLineEndPos - 1] == '\t'))
			{
				sLine.erase(nLineEndPos - 1);
				nLineEndPos--;
			}

			if (nType == RM_WS_BOTH)
			{
				while (sLine[0] == ' ' || sLine[0] == '\t')
					sLine.erase(0, 1);
			}

			Replace(this->PositionFromLine(i), this->GetLineEndPosition(i), sLine);
		}
	}

	EndUndoAction();
}


/////////////////////////////////////////////////
/// \brief Toggles a bookmark marker on the current line.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::toggleBookmark()
{
	int nLine = GetCurrentLine();

	if (MarkerOnLine(nLine, MARKER_BOOKMARK))
		MarkerDelete(nLine, MARKER_BOOKMARK);
	else
		MarkerAdd(nLine, MARKER_BOOKMARK);
}


/////////////////////////////////////////////////
/// \brief Removes all bookmark markers from the
/// current document.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::clearBookmarks()
{
	this->MarkerDeleteAll(MARKER_BOOKMARK);
}


/////////////////////////////////////////////////
/// \brief Sorts the selected lines alphabetically.
///
/// \param ascending bool
/// \return void
///
/// This function sorts the user selected lines
/// alphabetically in either ascending or descending
/// order. The sorting algorithm is case-insensitive.
/////////////////////////////////////////////////
void NumeReEditor::sortSelection(bool ascending)
{
	int nFirstline = 0;
	int nLastLine = GetLineCount() - 1;
	map<string, int> mSortMap;
	vector<wxString> vSortVector;
	string sCurrentLine;

	// Use the selection, if it has any
	if (HasSelection())
	{
		nFirstline = LineFromPosition(GetSelectionStart());
		nLastLine = LineFromPosition(GetSelectionEnd());
	}

	BeginUndoAction();

	// Copy the contents between starting and ending
	// line into a vector omitting the trailing line
	// ending characters. The contents are also converted
	// into a lowercase standard string and stored into a map
	for (int i = nFirstline; i <= nLastLine; i++)
	{
	    // Store the line in the vector
		vSortVector.push_back(this->GetLine(i));

        if (vSortVector[i - nFirstline].find_first_of("\r\n") != string::npos)
			vSortVector[i - nFirstline].erase(vSortVector[i - nFirstline].find_first_of("\r\n"));

		// Transform it to a lowercase standard string
		sCurrentLine = toLowerCase(vSortVector[i - nFirstline].ToStdString());
		StripSpaces(sCurrentLine);

		if (!sCurrentLine.length())
			sCurrentLine = " " + toString(i + 256);

		if (mSortMap.find(sCurrentLine) != mSortMap.end())
			sCurrentLine += "\n" + toString(i + 256); // need a value smaller than space therefore the \n

		// Store it in a map
		mSortMap[sCurrentLine] = i - nFirstline;
	}

	// Use the map auto-sorting feature to refill the section
	// with the sorted lines
	if (ascending)
	{
		for (auto iter = mSortMap.begin(); iter != mSortMap.end(); ++iter)
		{
			this->Replace(this->PositionFromLine(nFirstline), this->GetLineEndPosition(nFirstline), vSortVector[iter->second]);
			nFirstline++;
		}
	}
	else
	{
		for (auto iter = mSortMap.rbegin(); iter != mSortMap.rend(); ++iter)
		{
			this->Replace(this->PositionFromLine(nFirstline), this->GetLineEndPosition(nFirstline), vSortVector[iter->second]);
			nFirstline++;
		}
	}

	EndUndoAction();
}


/////////////////////////////////////////////////
/// \brief Gets the contents of the selected line.
///
/// \param nLine int
/// \return string
///
/// The contents of the selected line are returned
/// without leading and trailing whitespaces,
/// tabulators and line ending characters
/////////////////////////////////////////////////
string NumeReEditor::GetStrippedLine(int nLine)
{
	string sCurrentLine = this->GetLine(nLine).ToStdString();

	// Remove leading whitespaces
	if (sCurrentLine.find_first_not_of(" \t\r\n") == string::npos)
		return "";
	else
		sCurrentLine.erase(0, sCurrentLine.find_first_not_of(" \t\r\n"));

    // Remove trailing whitespaces
	if (sCurrentLine.find_last_not_of(" \r\t\n") != string::npos)
		sCurrentLine.erase(sCurrentLine.find_last_not_of(" \r\t\n") + 1);

	return sCurrentLine;
}


/////////////////////////////////////////////////
/// \brief Gets the contents of the selected range.
///
/// \param nPos1 int first character to extract
/// \param nPos2 int last character to extract
/// \param encode bool encode umlauts
/// \return string
///
/// The content between both positions is returned
/// without leading and trailing whitespaces,
/// tabulators and line ending characters. Carriage
/// returns in the middle of the selection are omitted
/// and only line endings are kept. If the encoding
/// option is activated, then umlauts are converted
/// into their two-letter representation.
/////////////////////////////////////////////////
string NumeReEditor::GetStrippedRange(int nPos1, int nPos2, bool encode)
{
	string sTextRange = this->GetTextRange(nPos1, nPos2).ToStdString();

	// Remove leading whitespaces
	while (sTextRange.front() == ' ' || sTextRange.front() == '\r' || sTextRange.front() == '\n' )
		sTextRange.erase(0, 1);

    // Remove trailing whitespaces
	while (sTextRange.back() == ' ' || sTextRange.back() == '\t' || sTextRange.back() == '\r' || sTextRange.back() == '\n')
		sTextRange.erase(sTextRange.length() - 1);

    // Convert CR LF into LF only
	while (sTextRange.find("\r\n") != string::npos)
		sTextRange.replace(sTextRange.find("\r\n"), 2, "\n");

    // Convert umlauts, if the encode flag
    // has been set
	if (encode)
	{
		for (size_t i = 0; i < sTextRange.length(); i++)
		{
			switch (sTextRange[i])
			{
				case '�':
					sTextRange.replace(i, 1, "Ae");
					break;
				case '�':
					sTextRange.replace(i, 1, "ae");
					break;
				case '�':
					sTextRange.replace(i, 1, "Oe");
					break;
				case '�':
					sTextRange.replace(i, 1, "oe");
					break;
				case '�':
					sTextRange.replace(i, 1, "Ue");
					break;
				case '�':
					sTextRange.replace(i, 1, "ue");
					break;
				case '�':
					sTextRange.replace(i, 1, "ss");
					break;
			}
		}
	}

	if (sTextRange.find_first_not_of('\t') == string::npos)
		return "";

	return sTextRange;
}


/////////////////////////////////////////////////
/// \brief Writes the content of the current code
/// file to a LaTeX file.
///
/// \param sLaTeXFileName const string&
/// \return bool
///
/// The contents of the current code file are
/// converted into a LaTeX file, where the code
/// sections are printed as listings and the
/// documentation strings are used as normal text.
/////////////////////////////////////////////////
bool NumeReEditor::writeLaTeXFile(const string& sLaTeXFileName)
{
	if (getFileType() != FILE_NSCR && getFileType() != FILE_NPRC)
		return false;

	string sFileContents;
	ofstream file_out;

	bool bTextMode = true;
	int startpos = 0;

	sFileContents += "% Created by NumeRe from the source of " + GetFileNameAndPath().ToStdString() + "\n\n";

    // Go through the whole file and convert the
    // syntax elements correspondingly
	for (int i = 0; i < GetLastPosition(); i++)
	{
	    // Determine the type of documentation,
	    // into which the current and the following
	    // characters shall be converted
		if (GetStyleAt(i) == wxSTC_NSCR_COMMENT_LINE && GetTextRange(i, i + 3) == "##!") // That's a documentation
		{
			if (!bTextMode)
			{
				if (i - startpos > 1)
					sFileContents += GetStrippedRange(startpos, i) + "\n";

				bTextMode = true;
				sFileContents += "\\end{lstlisting}\n";
			}

			sFileContents += parseDocumentation(i + 3, GetLineEndPosition(LineFromPosition(i))) + "\n";
			i = GetLineEndPosition(LineFromPosition(i)) + 1;
			startpos = i;
		}
		else if (GetStyleAt(i) == wxSTC_NSCR_COMMENT_LINE && GetTextRange(i, i + 3) == "##~") // ignore that (escaped comment)
		{
			if (i - startpos > 1)
				sFileContents += GetStrippedRange(startpos, i) + "\n";

			i = GetLineEndPosition(LineFromPosition(i)) + 1;
			startpos = i;
		}
		else if (GetStyleAt(i) == wxSTC_NSCR_COMMENT_BLOCK && GetTextRange(i, i + 3) == "#*!") // that's also a documentation
		{
			if (!bTextMode)
			{
				if (i - startpos > 1)
					sFileContents += GetStrippedRange(startpos, i) + "\n";

				bTextMode = true;
				sFileContents += "\\end{lstlisting}\n";
			}

			for (int j = i + 3; j < GetLastPosition(); j++)
			{
				if (GetStyleAt(j + 3) != wxSTC_NSCR_COMMENT_BLOCK || j + 1 == GetLastPosition())
				{
					sFileContents += parseDocumentation(i + 3, j) + "\n";
					i = j + 2;
					break;
				}
			}

			startpos = i;
		}
		else if (GetStyleAt(i) == wxSTC_NSCR_COMMENT_BLOCK && GetTextRange(i, i + 3) == "#**") // ignore that, that's also an escaped comment
		{
			if (i - startpos > 1)
				sFileContents += GetStrippedRange(startpos, i) + "\n";

			for (int j = i + 3; j < GetLastPosition(); j++)
			{
				if (GetStyleAt(j + 3) != wxSTC_NSCR_COMMENT_BLOCK || j + 1 == GetLastPosition())
				{
					i = j + 2;
					break;
				}
			}

			startpos = i + 1;
		}
		else // a normal code fragment
		{
			if (bTextMode)
			{
				startpos = i;
				bTextMode = false;
				sFileContents += "\\begin{lstlisting}\n";
			}

			if (i + 1 == GetLastPosition())
			{
				if (i - startpos > 1)
				{
					sFileContents += GetStrippedRange(startpos, i) + "\n";
				}
			}
		}
	}

	// Append a trailing \end{lstlisting}, if needed
	if (!bTextMode)
		sFileContents += "\\end{lstlisting}\n";

	if (!sFileContents.length())
		return false;

    // Write the converted documentation to the
    // target LaTeX file
	file_out.open(sLaTeXFileName.c_str());

	if (!file_out.good())
		return false;

	file_out << sFileContents;
	file_out.close();
	return true;
}


/////////////////////////////////////////////////
/// \brief Converts the documentation into LaTeX code.
///
/// \param nPos1 int
/// \param nPos2 int
/// \return string
///
/// The documentation extracted from the code
/// comments is converted to LaTeX text. This
/// includes:
/// \li unordered lists
/// \li german umlauts
/// \li inline code sequences
/////////////////////////////////////////////////
string NumeReEditor::parseDocumentation(int nPos1, int nPos2)
{
    // Get the text range
	string sTextRange = this->GetStrippedRange(nPos1, nPos2, false);

	// Handle unordered lists
	if (sTextRange.find("\n- ") != string::npos) // thats a unordered list
	{
		while (sTextRange.find("\n- ") != string::npos)
		{
			size_t nItemizeStart = sTextRange.find("\n- ");

			for (size_t i = nItemizeStart; i < sTextRange.length(); i++)
			{
				if (sTextRange.substr(i, 3) == "\n- ")
				{
					sTextRange.replace(i + 1, 1, "\\item");
					continue;
				}

				if ((sTextRange[i] == '\n' && sTextRange.substr(i, 3) != "\n  ") || i + 1 == sTextRange.length())
				{
					if (sTextRange[i] == '\n')
						sTextRange.insert(i, "\\end{itemize}");
					else
						sTextRange += "\\end{itemize}";

					sTextRange.insert(nItemizeStart + 1, "\\begin{itemize}");
					break;
				}
			}
		}
	}

	// Convert umlauts in LaTeX command sequences
	for (size_t i = 0; i < sTextRange.length(); i++)
	{
		switch (sTextRange[i])
		{
			case '�':
				sTextRange.replace(i, 1, "\\\"A");
				break;
			case '�':
				sTextRange.replace(i, 1, "\\\"a");
				break;
			case '�':
				sTextRange.replace(i, 1, "\\\"O");
				break;
			case '�':
				sTextRange.replace(i, 1, "\\\"o");
				break;
			case '�':
				sTextRange.replace(i, 1, "\\\"U");
				break;
			case '�':
				sTextRange.replace(i, 1, "\\\"u");
				break;
			case '�':
				sTextRange.replace(i, 1, "\\ss ");
				break;
		}
	}

	// Handle inline code sequences
	for (size_t i = 0; i < sTextRange.length(); i++)
	{
		if (sTextRange.substr(i, 2) == "!!")
		{
			for (size_t j = i + 2; j < sTextRange.length(); j++)
			{
				if (sTextRange.substr(j, 2) == "!!")
				{
					sTextRange.replace(j, 2, "`");
					sTextRange.replace(i, 2, "\\lstinline`");
					break;
				}
			}
		}
	}

	return sTextRange;
}


/////////////////////////////////////////////////
/// \brief Notifies the editor that the duplicated
/// code dialog had been closed.
///
/// \return void
///
/// The notification is done by setting the
/// corresponding pointer to al nullptr.
/////////////////////////////////////////////////
void NumeReEditor::notifyDialogClose()
{
	m_duplicateCode = nullptr;
}


/////////////////////////////////////////////////
/// \brief Changes the editor's font face.
///
/// \param font const wxFont& The new font face
/// \return void
///
/// The font face is changed globally and the
/// syntax highlighing is recalculated. Called
/// after the settings dialog was closed.
/////////////////////////////////////////////////
void NumeReEditor::SetEditorFont(const wxFont& font)
{
    wxFont newFont = font;
    StyleSetFont(wxSTC_STYLE_DEFAULT, newFont);
    StyleClearAll();
    UpdateSyntaxHighlighting(true);
}


/////////////////////////////////////////////////
/// \brief Returns true, if the selected setting
/// is active.
///
/// \param _setting EditorSettings
/// \return bool
///
/////////////////////////////////////////////////
bool NumeReEditor::getEditorSetting(EditorSettings _setting)
{
	return m_nEditorSetting & _setting;
}


/////////////////////////////////////////////////
/// \brief Enables or disables an editor setting.
///
/// \param _setting int
/// \return void
///
/// The selected setting is enabled, if it was
/// disabled and vice-versa. All necessary style
/// calculations are applied afterwards.
/////////////////////////////////////////////////
void NumeReEditor::ToggleSettings(int _setting)
{
	SetWhitespaceForeground(true, wxColor(170, 190, 210));
	SetWhitespaceSize(2);

	// Determine, whether the corresponding setting
	// is already enabled
	if (!(m_nEditorSetting & _setting))
	{
	    // Enable setting
		m_nEditorSetting |= _setting;

		// Apply the necessary style calculations
		if (_setting & SETTING_WRAPEOL)
		{
			SetWrapMode(wxSTC_WRAP_WORD);
			SetWrapIndentMode(wxSTC_WRAPINDENT_INDENT);
			SetWrapStartIndent(1);
			SetWrapVisualFlags(wxSTC_WRAPVISUALFLAG_END);
			SetWrapVisualFlagsLocation(wxSTC_WRAPVISUALFLAGLOC_END_BY_TEXT);
		}

		if (_setting & SETTING_DISPCTRLCHARS)
		{
			SetViewWhiteSpace(wxSTC_WS_VISIBLEALWAYS);
			SetViewEOL(true);
		}

		if (_setting & SETTING_USESECTIONS)
			markSections(true);
	}
	else
	{
	    // Disable setting
		m_nEditorSetting &= ~_setting;

		// Apply the necessary style calculations
		if (_setting == SETTING_WRAPEOL)
			SetWrapMode(wxSTC_WRAP_NONE);
		else if (_setting == SETTING_DISPCTRLCHARS)
		{
			SetViewEOL(false);
			SetViewWhiteSpace(wxSTC_WS_INVISIBLE);
		}
		else if (_setting == SETTING_USETXTADV)
		{
			SetIndicatorCurrent(HIGHLIGHT_STRIKETHROUGH);
			IndicatorClearRange(0, GetLastPosition());
		}
		else if (_setting == SETTING_USESECTIONS)
			MarkerDeleteAll(MARKER_SECTION);
	}

	UpdateSyntaxHighlighting();
	m_analyzer->run();
}


/////////////////////////////////////////////////
/// \brief Finds and highlights the matching brace.
///
/// \param nPos int
/// \return void
///
/// This function searches the matching brace to
/// the one at the selected position and performs
/// the highlighting. If no matching brace is found,
/// the single brace is highlighted correspondingly.
/////////////////////////////////////////////////
void NumeReEditor::getMatchingBrace(int nPos)
{
	int nMatch = BraceMatch(nPos);

	// Search the matching brace
	if (nMatch == wxSTC_INVALID_POSITION)
		BraceBadLight(nPos);
	else
	{
	    // If one is found, then highlight the
	    // the brace and the room in between
		SetIndicatorCurrent(HIGHLIGHT_MATCHING_BRACE);
		IndicatorClearRange(0, GetLastPosition());
		IndicatorSetStyle(HIGHLIGHT_MATCHING_BRACE, wxSTC_INDIC_DIAGONAL);
		IndicatorSetForeground(HIGHLIGHT_MATCHING_BRACE, wxColour(0, 150, 0));

		if (nMatch < nPos)
		{
			BraceHighlight(nMatch, nPos);
			IndicatorFillRange(nMatch + 1, nPos - nMatch - 1);
		}
		else
		{
			BraceHighlight(nPos, nMatch);
			IndicatorFillRange(nPos + 1, nMatch - nPos - 1);
		}
	}
}


/////////////////////////////////////////////////
/// \brief Finds and highlights the matching flow
/// control statements.
///
/// \param nPos int
/// \return void
///
/// This function searches the matching flow control
/// statement to the one at the selected position
/// and performs the highlighting. If no matching
/// statement or part of it is missing, then the
/// remaining statements are highlighted
/// correspondingly.
/////////////////////////////////////////////////
void NumeReEditor::getMatchingBlock(int nPos)
{
    // Search all flow control statements
	vector<int> vMatch = BlockMatch(nPos);

	if (vMatch.size() == 1 && vMatch[0] == wxSTC_INVALID_POSITION)
		return;

    // Select the correct indicator for available
    // and missing blocks
	if (vMatch.front() == wxSTC_INVALID_POSITION || vMatch.back() == wxSTC_INVALID_POSITION)
		SetIndicatorCurrent(HIGHLIGHT_NOT_MATCHING_BLOCK);
	else
		SetIndicatorCurrent(HIGHLIGHT_MATCHING_BLOCK);

    // Clear the indicators
	IndicatorClearRange(0, GetLastPosition());
	IndicatorSetStyle(HIGHLIGHT_MATCHING_BLOCK, wxSTC_INDIC_ROUNDBOX);
	IndicatorSetAlpha(HIGHLIGHT_MATCHING_BLOCK, 100);
	IndicatorSetForeground(HIGHLIGHT_MATCHING_BLOCK, wxColour(0, 220, 0));
	IndicatorSetStyle(HIGHLIGHT_NOT_MATCHING_BLOCK, wxSTC_INDIC_ROUNDBOX);
	IndicatorSetAlpha(HIGHLIGHT_NOT_MATCHING_BLOCK, 128);
	IndicatorSetForeground(HIGHLIGHT_NOT_MATCHING_BLOCK, wxColour(255, 0, 0));

	// Highlight all occurences
	for (size_t i = 0; i < vMatch.size(); i++)
	{
		if (vMatch[i] == wxSTC_INVALID_POSITION)
			continue;

		IndicatorFillRange(vMatch[i], WordEndPosition(vMatch[i], true) - vMatch[i]);
	}
}


/////////////////////////////////////////////////
/// \brief Finds all matching flow control statements.
///
/// \param nPos int
/// \return vector<int>
///
/// Returnes a vector. If first element is invalid,
/// then the word at the position is no command.
/// If the last one is invalid, then there's no
/// matching partner. It returnes more than two
/// elements for "if" and "switch" blocks.
/// If there's no first "if" and if one currently
/// is focusing on an "else...", the first element
/// may be invalid, but more can be returned.
/////////////////////////////////////////////////
vector<int> NumeReEditor::BlockMatch(int nPos)
{
    // Select the correct helper function
	if (this->getFileType() == FILE_NSCR || this->getFileType() == FILE_NPRC)
		return BlockMatchNSCR(nPos);
	else if (this->getFileType() == FILE_MATLAB)
		return BlockMatchMATLAB(nPos);
	else
	{
		vector<int> vPos;
		vPos.push_back(wxSTC_INVALID_POSITION);
		return vPos;
	}
}


/////////////////////////////////////////////////
/// \brief Finds all matching flow control statements
/// for NumeRe command syntax.
///
/// \param nPos int
/// \return vector<int>
///
/// See description of \c BlockMatch
/////////////////////////////////////////////////
vector<int> NumeReEditor::BlockMatchNSCR(int nPos)
{
	int nFor = 0;
	int nIf = 0;
	int nWhile = 0;
	int nCompose = 0;
	int nProcedure = 0;
	int nSwitch = 0;
	int nStartPos = WordStartPosition(nPos, true);
	vector<int> vPos;
	wxString startblock;
	wxString endblock;
	bool bSearchForIf = false; //if we search for an if block element. If yes => also mark the "else..." parts.
	bool bSearchForSwitch = false; //if we search for an switch block element. If yes => also mark the "case..." parts.
	int nSearchDir = 1; //direction in which to search for the matching block partner

	if (GetStyleAt(nPos) != wxSTC_NSCR_COMMAND && GetStyleAt(nPos) != wxSTC_NPRC_COMMAND)
	{
		if (nPos && GetStyleAt(nPos - 1) == wxSTC_NSCR_COMMAND)
			nPos--;
		else
		{
			vPos.push_back(wxSTC_INVALID_POSITION);
			return vPos;
		}
	}


	startblock = GetTextRange(WordStartPosition(nPos, true), WordEndPosition(nPos, true));

    if (startblock.substr(0, 3) == "end")
	{
		endblock = startblock.substr(3);
		nSearchDir = -1;
	}
	else if (startblock == "else" || startblock == "elseif")
	{
		// search for starting "if"
		// adding 1 to nIf, because we're already inside of an "if"
		nIf++;

		for (int i = WordEndPosition(nPos, true); i >= 0; i--)
		{
			if (GetStyleAt(i) == wxSTC_NSCR_COMMAND)
			{
				wxString currentWord = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

				if (currentWord == "for")
					nFor--; //if we iterate upwards, the closing blocks shall increment and the opening blocks decrement the counter
				else if (currentWord == "endfor")
					nFor++;
				else if (currentWord == "while")
					nWhile--;
				else if (currentWord == "endwhile")
					nWhile++;
				else if (currentWord == "if")
					nIf--;
				else if (currentWord == "endif")
					nIf++;
				else if (currentWord == "compose")
					nCompose--;
				else if (currentWord == "endcompose")
					nCompose++;
				else if (currentWord == "procedure")
					nProcedure--;
				else if (currentWord == "endprocedure")
					nProcedure++;
				else if (currentWord == "switch")
					nSwitch--;
				else if (currentWord == "endswitch")
					nSwitch++;

				if (currentWord == "if" && !nFor && !nIf && !nWhile && !nCompose && !nProcedure && !nSwitch)
				{
					nStartPos = WordStartPosition(i, true);
					break;
				}

				i -= currentWord.length();
			}

			if (nFor < 0 || nWhile < 0 || nIf < 0 || nCompose < 0 || nProcedure < 0 || nSwitch < 0)
			{
				// There's no matching partner
				// set the first to invalid but do not return
				vPos.push_back(wxSTC_INVALID_POSITION);
				break;
			}
		}

		if (nFor > 0 || nWhile > 0 || nIf > 0 || nCompose > 0 || nProcedure > 0 || nSwitch > 0)
		{
			// There's no matching partner
			// set the first to invalid but do not return
			vPos.push_back(wxSTC_INVALID_POSITION);
			nIf = 1;
		}
		else
			nIf = 0;

		nFor = 0;
		nWhile = 0;
		nCompose = 0;
		nProcedure = 0;
		nSwitch = 0;

		bSearchForIf = true;
		endblock = "endif";
	}
	else if (startblock == "case" || startblock == "default")
	{
		// search for starting "switch"
		// adding 1 to nSwitch, because we're already inside of an "switch"
		nSwitch++;

		for (int i = WordEndPosition(nPos, true); i >= 0; i--)
		{
			if (GetStyleAt(i) == wxSTC_NSCR_COMMAND)
			{
				wxString currentWord = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

				if (currentWord == "for")
					nFor--; //if we iterate upwards, the closing blocks shall increment and the opening blocks decrement the counter
				else if (currentWord == "endfor")
					nFor++;
				else if (currentWord == "while")
					nWhile--;
				else if (currentWord == "endwhile")
					nWhile++;
				else if (currentWord == "if")
					nIf--;
				else if (currentWord == "endif")
					nIf++;
				else if (currentWord == "compose")
					nCompose--;
				else if (currentWord == "endcompose")
					nCompose++;
				else if (currentWord == "procedure")
					nProcedure--;
				else if (currentWord == "endprocedure")
					nProcedure++;
				else if (currentWord == "switch")
					nSwitch--;
				else if (currentWord == "endswitch")
					nSwitch++;

				if (currentWord == "switch" && !nFor && !nIf && !nWhile && !nCompose && !nProcedure && !nSwitch)
				{
					nStartPos = WordStartPosition(i, true);
					break;
				}

				i -= currentWord.length();
			}

			if (nFor < 0 || nWhile < 0 || nIf < 0 || nCompose < 0 || nProcedure < 0 || nSwitch < 0)
			{
				// There's no matching partner
				// set the first to invalid but do not return
				vPos.push_back(wxSTC_INVALID_POSITION);
				break;
			}
		}

		if (nFor > 0 || nWhile > 0 || nIf > 0 || nCompose > 0 || nProcedure > 0 || nSwitch > 0)
		{
			// There's no matching partner
			// set the first to invalid but do not return
			vPos.push_back(wxSTC_INVALID_POSITION);
			nIf = 1;
		}
		else
			nIf = 0;

		nFor = 0;
		nWhile = 0;
		nCompose = 0;
		nProcedure = 0;
		nSwitch = 0;

		bSearchForSwitch = true;
		endblock = "endswitch";
	}
	else if (startblock == "if" || startblock == "for" || startblock == "while" || startblock == "compose" || startblock == "procedure" || startblock == "switch")
		endblock = "end" + startblock;
	else
	{
		vPos.push_back(wxSTC_INVALID_POSITION);
		return vPos;
	}

	if (startblock == "if" || endblock == "if")
		bSearchForIf = true;
	else if (startblock == "switch" || endblock == "switch")
		bSearchForSwitch = true;

	vPos.push_back(nStartPos);

	if (nSearchDir == -1)
		nStartPos = WordEndPosition(nPos, true);

	for (int i = nStartPos; (i < GetLastPosition() && i >= 0); i += nSearchDir) // iterates down, if nSearchDir == 1, and up of nSearchDir == -1
	{
		if (GetStyleAt(i) == wxSTC_NSCR_COMMAND)
		{
			wxString currentWord = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

			if (currentWord == "for")
				nFor += nSearchDir; //if we iterate upwards, the closing blocks shall increment and the opening blocks decrement the counter
			else if (currentWord == "endfor")
				nFor -= nSearchDir;
			else if (currentWord == "while")
				nWhile += nSearchDir;
			else if (currentWord == "endwhile")
				nWhile -= nSearchDir;
			else if (currentWord == "if")
				nIf += nSearchDir;
			else if (currentWord == "endif")
				nIf -= nSearchDir;
			else if (currentWord == "compose")
				nCompose += nSearchDir;
			else if (currentWord == "endcompose")
				nCompose -= nSearchDir;
			else if (currentWord == "procedure")
				nProcedure += nSearchDir;
			else if (currentWord == "endprocedure")
				nProcedure -= nSearchDir;
			else if (currentWord == "switch")
				nSwitch += nSearchDir;
			else if (currentWord == "endswitch")
				nSwitch -= nSearchDir;

			if (bSearchForIf && nIf == 1 && !nFor && !nWhile && !nProcedure && !nCompose && !nSwitch // only in the current if block
					&& (currentWord == "else" || currentWord == "elseif"))
				vPos.push_back(WordStartPosition(i, true));

			if (bSearchForSwitch && nSwitch == 1 && !nFor && !nWhile && !nProcedure && !nCompose && !nIf // only in the current if block
					&& (currentWord == "case" || currentWord == "default"))
				vPos.push_back(WordStartPosition(i, true));

			if (currentWord == endblock && !nFor && !nIf && !nWhile && !nProcedure && !nCompose && !nSwitch)
			{
				vPos.push_back(WordStartPosition(i, true));
				break;
			}

			i += nSearchDir * currentWord.length();
		}

		if (nFor < 0 || nWhile < 0 || nIf < 0 || nProcedure < 0 || nCompose < 0 || nSwitch < 0)
		{
			// There's no matching partner
			vPos.push_back(wxSTC_INVALID_POSITION);
			break;
		}
	}

	if (!vPos.size()
			|| (nFor > 0 || nWhile > 0 || nIf > 0 || nProcedure > 0 || nCompose > 0 || nSwitch > 0))
		vPos.push_back(wxSTC_INVALID_POSITION);

	return vPos;
}


/////////////////////////////////////////////////
/// \brief Finds all matching flow control statements
/// for MATLAB command syntax.
///
/// \param nPos int
/// \return vector<int>
///
/// See description of \c BlockMatch
/////////////////////////////////////////////////
vector<int> NumeReEditor::BlockMatchMATLAB(int nPos)
{
	int nBlock = 0;

	int nStartPos = WordStartPosition(nPos, true);
	vector<int> vPos;
	wxString startblock;
	wxString endblock;
	bool bSearchForIf = false; //if we search for an if block element. If yes => also mark the "else..." parts.
	bool bSearchForSwitch = false;
	bool bSearchForCatch = false;
	int nSearchDir = 1; //direction in which to search for the matching block partner

	if (GetStyleAt(nPos) != wxSTC_MATLAB_KEYWORD)
	{
		if (nPos && GetStyleAt(nPos - 1) == wxSTC_MATLAB_KEYWORD)
			nPos--;
		else
		{
			vPos.push_back(wxSTC_INVALID_POSITION);
			return vPos;
		}
	}


	startblock = GetTextRange(WordStartPosition(nPos, true), WordEndPosition(nPos, true));

	if (startblock == "end")
	{
		// search for starting block
		// adding 1 to nBlock, because we're already inside of an "block"
		//nBlock++;
		for (int i = WordStartPosition(nPos, true); i >= 0; i--)
		{
			if (GetStyleAt(i) == wxSTC_MATLAB_KEYWORD)
			{
				wxString currentWord = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

				if (currentWord == "for"
						|| currentWord == "while"
						|| currentWord == "function"
						|| currentWord == "if"
						|| currentWord == "switch"
						|| currentWord == "try"
						|| currentWord == "classdef"
						|| currentWord == "properties"
						|| currentWord == "methods")
					nBlock--;
				else if (currentWord == "end")
					nBlock++;

				if (!nBlock)
				{
					nStartPos = WordStartPosition(i, true);
					startblock = currentWord;
					if (currentWord == "if")
						bSearchForIf = true;
					if (currentWord == "switch")
						bSearchForSwitch = true;
					if (currentWord == "try")
						bSearchForCatch = true;
					break;
				}

				i -= currentWord.length();
			}

			if (nBlock < 0)
			{
				// There's no matching partner
				// set the first to invalid but do not return
				vPos.push_back(wxSTC_INVALID_POSITION);
				break;
			}
		}

		endblock = "end";
	}
	else if (startblock == "else" || startblock == "elseif")
	{
		// search for starting "if"
		// adding 1 to nBlock, because we're already inside of an "if"
		nBlock++;
		for (int i = WordEndPosition(nPos, true); i >= 0; i--)
		{
			if (GetStyleAt(i) == wxSTC_MATLAB_KEYWORD)
			{
				wxString currentWord = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

				if (currentWord == "for"
						|| currentWord == "while"
						|| currentWord == "function"
						|| currentWord == "if"
						|| currentWord == "switch"
						|| currentWord == "try"
						|| currentWord == "classdef"
						|| currentWord == "properties"
						|| currentWord == "methods")
					nBlock--;
				else if (currentWord == "end")
					nBlock++;

				if (currentWord == "if" && !nBlock)
				{
					nStartPos = WordStartPosition(i, true);
					startblock = "if";
					break;
				}

				i -= currentWord.length();
			}

			if (nBlock < 0)
			{
				// There's no matching partner
				// set the first to invalid but do not return
				vPos.push_back(wxSTC_INVALID_POSITION);
				break;
			}
		}

		if (nBlock > 0)
		{
			// There's no matching partner
			// set the first to invalid but do not return
			vPos.push_back(wxSTC_INVALID_POSITION);
			nBlock = 1;
		}
		else
			nBlock = 0;

		bSearchForIf = true;
		endblock = "end";
	}
	else if (startblock == "case" || startblock == "otherwise")
	{
		// search for starting "if"
		// adding 1 to nBlock, because we're already inside of an "if"
		nBlock++;

		for (int i = WordEndPosition(nPos, true); i >= 0; i--)
		{
			if (GetStyleAt(i) == wxSTC_MATLAB_KEYWORD)
			{
				wxString currentWord = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

				if (currentWord == "for"
						|| currentWord == "while"
						|| currentWord == "function"
						|| currentWord == "if"
						|| currentWord == "switch"
						|| currentWord == "try"
						|| currentWord == "classdef"
						|| currentWord == "properties"
						|| currentWord == "methods")
					nBlock--;
				else if (currentWord == "end")
					nBlock++;

				if (currentWord == "switch" && !nBlock)
				{
					nStartPos = WordStartPosition(i, true);
					startblock = "switch";
					break;
				}

				i -= currentWord.length();
			}

			if (nBlock < 0)
			{
				// There's no matching partner
				// set the first to invalid but do not return
				vPos.push_back(wxSTC_INVALID_POSITION);
				break;
			}
		}

		if (nBlock > 0)
		{
			// There's no matching partner
			// set the first to invalid but do not return
			vPos.push_back(wxSTC_INVALID_POSITION);
			nBlock = 1;
		}
		else
			nBlock = 0;

		bSearchForSwitch = true;
		endblock = "end";
	}
	else if (startblock == "catch")
	{
		// search for starting "catch"
		// adding 1 to nBlock, because we're already inside of an "if"
		nBlock++;

		for (int i = WordEndPosition(nPos, true); i >= 0; i--)
		{
			if (GetStyleAt(i) == wxSTC_MATLAB_KEYWORD)
			{
				wxString currentWord = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

				if (currentWord == "for"
						|| currentWord == "while"
						|| currentWord == "function"
						|| currentWord == "if"
						|| currentWord == "switch"
						|| currentWord == "try"
						|| currentWord == "classdef"
						|| currentWord == "properties"
						|| currentWord == "methods")
					nBlock--;
				else if (currentWord == "end")
					nBlock++;

				if (currentWord == "try" && !nBlock)
				{
					nStartPos = WordStartPosition(i, true);
					startblock = "try";
					break;
				}

				i -= currentWord.length();
			}

			if (nBlock < 0)
			{
				// There's no matching partner
				// set the first to invalid but do not return
				vPos.push_back(wxSTC_INVALID_POSITION);
				break;
			}
		}

		if (nBlock > 0)
		{
			// There's no matching partner
			// set the first to invalid but do not return
			vPos.push_back(wxSTC_INVALID_POSITION);
			nBlock = 1;
		}
		else
			nBlock = 0;

		bSearchForCatch = true;
		endblock = "end";
	}

	if (startblock == "for"
			|| startblock == "while"
			|| startblock == "function"
			|| startblock == "if"
			|| startblock == "switch"
			|| startblock == "try"
			|| startblock == "classdef"
			|| startblock == "properties"
			|| startblock == "methods")
		endblock = "end";
	else
	{
		vPos.push_back(wxSTC_INVALID_POSITION);
		return vPos;
	}

	if (startblock == "if" || endblock == "if")
		bSearchForIf = true;

	if (startblock == "switch" || endblock == "switch")
		bSearchForSwitch = true;

	if (startblock == "try" || endblock == "try")
		bSearchForCatch = true;

	vPos.push_back(nStartPos);

	if (nSearchDir == -1)
		nStartPos = WordEndPosition(nPos, true);

	for (int i = nStartPos; (i < GetLastPosition() && i >= 0); i += nSearchDir) // iterates down, if nSearchDir == 1, and up of nSearchDir == -1
	{
		if (GetStyleAt(i) == wxSTC_MATLAB_KEYWORD)
		{
			wxString currentWord = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

			if (currentWord == "for"
					|| currentWord == "while"
					|| currentWord == "function"
					|| currentWord == "if"
					|| currentWord == "switch"
					|| currentWord == "try"
					|| currentWord == "classdef"
					|| currentWord == "properties"
					|| currentWord == "methods")
				nBlock += nSearchDir; //if we iterate upwards, the closing blocks shall increment and the opening blocks decrement the counter
			else if (currentWord == "end")
				nBlock -= nSearchDir;

			if (bSearchForIf && nBlock == 1 // only in the current if block
					&& (currentWord == "else" || currentWord == "elseif"))
				vPos.push_back(WordStartPosition(i, true));

			if (bSearchForSwitch && nBlock == 1 // only in the current if block
					&& (currentWord == "case" || currentWord == "otherwise"))
				vPos.push_back(WordStartPosition(i, true));

			if (bSearchForCatch && nBlock == 1 // only in the current if block
					&& currentWord == "catch")
				vPos.push_back(WordStartPosition(i, true));

			if (currentWord == endblock && !nBlock)
			{
				vPos.push_back(WordStartPosition(i, true));
				break;
			}

			i += nSearchDir * currentWord.length();
		}

		if (nBlock < 0)
		{
			// There's no matching partner
			vPos.push_back(wxSTC_INVALID_POSITION);
			break;
		}
	}

	if (!vPos.size()
			|| (nBlock > 0))
		vPos.push_back(wxSTC_INVALID_POSITION);

	return vPos;
}


//////////////////////////////////////////////////////////////////////////////
///  public HasBeenSaved
///  Checks if the editor has been saved
///
///  @return bool Whether or not the editor has been saved
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
bool NumeReEditor::HasBeenSaved()
{
	bool result = m_fileNameAndPath.GetFullPath() != wxEmptyString;
	return result;// && !m_bSetUnsaved;
}


/////////////////////////////////////////////////
/// \brief Marks the editor as modified.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::SetUnsaved()
{
	m_bSetUnsaved = true;
}


//////////////////////////////////////////////////////////////////////////////
///  public UpdateSyntaxHighlighting
///  Sets up the editor's syntax highlighting
///
///  @return void
///
///  @remarks Currently only called on creation.  If syntax highlighting customization was
///  @remarks allowed, this is where the user's choices would be used
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::UpdateSyntaxHighlighting(bool forceUpdate)
{
	wxString filename = GetFileNameAndPath();

	this->StyleSetBackground(wxSTC_STYLE_DEFAULT, m_options->GetSyntaxStyle(Options::STANDARD).background);

	FileFilterType filetype = m_project->GetFileType(filename);

	if (m_fileType != filetype)
		m_fileType = filetype;
	else if (!forceUpdate && (m_fileType == FILE_NSCR
							  || m_fileType == FILE_NPRC
							  || m_fileType == FILE_TEXSOURCE
							  || m_fileType == FILE_DATAFILES
							  || m_fileType == FILE_MATLAB
							  || m_fileType == FILE_CPP))
		return;


	// make it for both: NSCR and NPRC
	if (filetype == FILE_NSCR || filetype == FILE_NPRC || filetype == FILE_MATLAB || filetype == FILE_CPP || filetype == FILE_DIFF)
	{
		SetFoldFlags(wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);

		SetMarginType(MARGIN_FOLD, wxSTC_MARGIN_SYMBOL);
		SetMarginWidth(MARGIN_FOLD, 13);
		SetMarginMask(MARGIN_FOLD, wxSTC_MASK_FOLDERS);
		SetMarginSensitive(MARGIN_FOLD, true);
		StyleSetBackground(MARGIN_FOLD, wxColor(200, 200, 200) );
		SetMarginSensitive(MARGIN_FOLD, true);

		wxColor grey( 100, 100, 100 );
		MarkerDefine (wxSTC_MARKNUM_FOLDER, wxSTC_MARK_BOXPLUS);
		MarkerSetForeground (wxSTC_MARKNUM_FOLDER, "WHITE");
		MarkerSetBackground (wxSTC_MARKNUM_FOLDER, grey);

		MarkerDefine (wxSTC_MARKNUM_FOLDEROPEN,    wxSTC_MARK_BOXMINUS);
		MarkerSetForeground (wxSTC_MARKNUM_FOLDEROPEN, "WHITE");
		MarkerSetBackground (wxSTC_MARKNUM_FOLDEROPEN, grey);

		MarkerDefine (wxSTC_MARKNUM_FOLDERSUB,     wxSTC_MARK_VLINE);
		MarkerSetForeground (wxSTC_MARKNUM_FOLDERSUB, grey);
		MarkerSetBackground (wxSTC_MARKNUM_FOLDERSUB, grey);

		MarkerDefine (wxSTC_MARKNUM_FOLDEREND,     wxSTC_MARK_BOXPLUSCONNECTED);
		MarkerSetForeground (wxSTC_MARKNUM_FOLDEREND, "WHITE");
		MarkerSetBackground (wxSTC_MARKNUM_FOLDEREND, grey);

		MarkerDefine (wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARK_BOXMINUSCONNECTED);
		MarkerSetForeground (wxSTC_MARKNUM_FOLDEROPENMID, "WHITE");
		MarkerSetBackground (wxSTC_MARKNUM_FOLDEROPENMID, grey);

		MarkerDefine (wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_TCORNER);
		MarkerSetForeground (wxSTC_MARKNUM_FOLDERMIDTAIL, grey);
		MarkerSetBackground (wxSTC_MARKNUM_FOLDERMIDTAIL, grey);

		MarkerDefine (wxSTC_MARKNUM_FOLDERTAIL,    wxSTC_MARK_LCORNER);
		MarkerSetForeground (wxSTC_MARKNUM_FOLDERTAIL, grey);
		MarkerSetBackground (wxSTC_MARKNUM_FOLDERTAIL, grey);

		MarkerEnableHighlight(true);
	}

	if (filetype == FILE_NSCR)
	{
		m_fileType = FILE_NSCR;
		this->SetLexer(wxSTC_LEX_NSCR);
		this->SetProperty("fold", "1");
		if (_syntax)
		{
			this->SetKeyWords(0, _syntax->getCommands());
			this->SetKeyWords(1, _syntax->getOptions());
			this->SetKeyWords(2, _syntax->getFunctions());
			this->SetKeyWords(3, _syntax->getMethods());
			this->SetKeyWords(4, "x y z t");
			this->SetKeyWords(5, _syntax->getConstants());
			this->SetKeyWords(6, _syntax->getSpecial());
			this->SetKeyWords(7, _syntax->getOperators());
			this->SetKeyWords(8, _syntax->getNPRCCommands());
		}

		for (int i = 0; i <= wxSTC_NSCR_PROCEDURE_COMMANDS; i++)
		{
			SyntaxStyles _style;
			switch (i)
			{
				case wxSTC_NSCR_DEFAULT:
				case wxSTC_NSCR_IDENTIFIER:
					_style = m_options->GetSyntaxStyle(Options::STANDARD);
					break;
				case wxSTC_NSCR_NUMBERS:
					_style = m_options->GetSyntaxStyle(Options::NUMBER);
					break;
				case wxSTC_NSCR_COMMENT_BLOCK:
				case wxSTC_NSCR_COMMENT_LINE:
					_style = m_options->GetSyntaxStyle(Options::COMMENT);
					break;
				case wxSTC_NSCR_COMMAND:
					_style = m_options->GetSyntaxStyle(Options::COMMAND);
					break;
				case wxSTC_NSCR_OPTION:
					_style = m_options->GetSyntaxStyle(Options::OPTION);
					break;
				case wxSTC_NSCR_CONSTANTS:
					_style = m_options->GetSyntaxStyle(Options::CONSTANT);
					break;
				case wxSTC_NSCR_FUNCTION:
					_style = m_options->GetSyntaxStyle(Options::FUNCTION);
					break;
				case wxSTC_NSCR_METHOD:
					_style = m_options->GetSyntaxStyle(Options::METHODS);
					break;
				case wxSTC_NSCR_PREDEFS:
					_style = m_options->GetSyntaxStyle(Options::SPECIALVAL);
					break;
				case wxSTC_NSCR_STRING:
					_style = m_options->GetSyntaxStyle(Options::STRING);
					break;
				case wxSTC_NSCR_STRING_PARSER:
					_style = m_options->GetSyntaxStyle(Options::STRINGPARSER);
					break;
				case wxSTC_NSCR_INCLUDES:
					_style = m_options->GetSyntaxStyle(Options::INCLUDES);
					break;
				case wxSTC_NSCR_PROCEDURES:
					_style = m_options->GetSyntaxStyle(Options::PROCEDURE);
					break;
				case wxSTC_NSCR_PROCEDURE_COMMANDS:
					_style = m_options->GetSyntaxStyle(Options::PROCEDURE_COMMAND);
					break;
				case wxSTC_NSCR_INSTALL:
					_style = m_options->GetSyntaxStyle(Options::INSTALL);
					break;
				case wxSTC_NSCR_DEFAULT_VARS:
					_style = m_options->GetSyntaxStyle(Options::DEFAULT_VARS);
					break;
				case wxSTC_NSCR_CUSTOM_FUNCTION:
					_style = m_options->GetSyntaxStyle(Options::CUSTOM_FUNCTION);
					break;
				case wxSTC_NSCR_CLUSTER:
					_style = m_options->GetSyntaxStyle(Options::CLUSTER);
					break;
				case wxSTC_NSCR_OPERATORS:
				case wxSTC_NSCR_OPERATOR_KEYWORDS:
					_style = m_options->GetSyntaxStyle(Options::OPERATOR);
					break;
			}

			this->StyleSetForeground(i, _style.foreground);
			if (!_style.defaultbackground)
				this->StyleSetBackground(i, _style.background);
			else
				this->StyleSetBackground(i, this->StyleGetBackground(wxSTC_STYLE_DEFAULT));
			this->StyleSetBold(i, _style.bold);
			this->StyleSetItalic(i, _style.italics);
			this->StyleSetUnderline(i, _style.underline);
		}
	}
	else if (filetype == FILE_NPRC)
	{
		m_fileType = FILE_NPRC;
		this->SetLexer(wxSTC_LEX_NPRC);
		this->SetProperty("fold", "1");
		if (_syntax)
		{
			this->SetKeyWords(0, _syntax->getCommands() + _syntax->getNPRCCommands());
			this->SetKeyWords(1, _syntax->getOptions());
			this->SetKeyWords(2, _syntax->getFunctions());
			this->SetKeyWords(3, _syntax->getMethods());
			this->SetKeyWords(4, "x y z t");
			this->SetKeyWords(5, _syntax->getConstants());
			this->SetKeyWords(6, _syntax->getSpecial());
			this->SetKeyWords(7, _syntax->getOperators());
		}


		for (int i = 0; i <= wxSTC_NPRC_FLAGS; i++)
		{
			SyntaxStyles _style;
			switch (i)
			{
				case wxSTC_NPRC_DEFAULT:
				case wxSTC_NPRC_IDENTIFIER:
					_style = m_options->GetSyntaxStyle(Options::STANDARD);
					break;
				case wxSTC_NPRC_NUMBERS:
					_style = m_options->GetSyntaxStyle(Options::NUMBER);
					break;
				case wxSTC_NPRC_COMMENT_BLOCK:
				case wxSTC_NPRC_COMMENT_LINE:
					_style = m_options->GetSyntaxStyle(Options::COMMENT);
					break;
				case wxSTC_NPRC_COMMAND:
					_style = m_options->GetSyntaxStyle(Options::COMMAND);
					break;
				case wxSTC_NPRC_OPTION:
					_style = m_options->GetSyntaxStyle(Options::OPTION);
					break;
				case wxSTC_NPRC_CONSTANTS:
					_style = m_options->GetSyntaxStyle(Options::CONSTANT);
					break;
				case wxSTC_NPRC_FUNCTION:
					_style = m_options->GetSyntaxStyle(Options::FUNCTION);
					break;
				case wxSTC_NPRC_METHOD:
					_style = m_options->GetSyntaxStyle(Options::METHODS);
					break;
				case wxSTC_NPRC_PREDEFS:
					_style = m_options->GetSyntaxStyle(Options::SPECIALVAL);
					break;
				case wxSTC_NPRC_STRING:
					_style = m_options->GetSyntaxStyle(Options::STRING);
					break;
				case wxSTC_NPRC_STRING_PARSER:
					_style = m_options->GetSyntaxStyle(Options::STRINGPARSER);
					break;
				case wxSTC_NPRC_INCLUDES:
					_style = m_options->GetSyntaxStyle(Options::INCLUDES);
					break;
				case wxSTC_NPRC_PROCEDURES:
				case wxSTC_NPRC_FLAGS:
					_style = m_options->GetSyntaxStyle(Options::PROCEDURE);
					break;
				case wxSTC_NPRC_DEFAULT_VARS:
					_style = m_options->GetSyntaxStyle(Options::DEFAULT_VARS);
					break;
				case wxSTC_NPRC_CUSTOM_FUNCTION:
					_style = m_options->GetSyntaxStyle(Options::CUSTOM_FUNCTION);
					break;
				case wxSTC_NPRC_CLUSTER:
					_style = m_options->GetSyntaxStyle(Options::CLUSTER);
					break;
				case wxSTC_NPRC_OPERATORS:
				case wxSTC_NPRC_OPERATOR_KEYWORDS:
					_style = m_options->GetSyntaxStyle(Options::OPERATOR);
					break;
			}

			this->StyleSetForeground(i, _style.foreground);
			if (!_style.defaultbackground)
				this->StyleSetBackground(i, _style.background);
			else
				this->StyleSetBackground(i, this->StyleGetBackground(wxSTC_STYLE_DEFAULT));
			this->StyleSetBold(i, _style.bold);
			this->StyleSetItalic(i, _style.italics);
			this->StyleSetUnderline(i, _style.underline);
		}
	}
	else if (filetype == FILE_TEXSOURCE)
	{
		SetLexer(wxSTC_LEX_TEX);
		StyleSetForeground(wxSTC_TEX_DEFAULT, wxColor(0, 128, 0)); //Comment
		StyleSetForeground(wxSTC_TEX_COMMAND, wxColor(0, 0, 255)); //Command
		StyleSetBold(wxSTC_TEX_COMMAND, true);
		StyleSetUnderline(wxSTC_TEX_COMMAND, false);
		StyleSetForeground(wxSTC_TEX_TEXT, wxColor(0, 0, 0)); // Actual text
		StyleSetForeground(wxSTC_TEX_GROUP, wxColor(0, 128, 0)); // Grouping elements like $ $ or { }
		StyleSetBackground(wxSTC_TEX_GROUP, wxColor(255, 255, 183)); // Grouping elements like $ $ or { }
		StyleSetBold(wxSTC_TEX_GROUP, true);
		StyleSetForeground(wxSTC_TEX_SPECIAL, wxColor(255, 0, 196)); // Parentheses/Brackets
		StyleSetItalic(wxSTC_TEX_SPECIAL, false);
		StyleSetBold(wxSTC_TEX_SPECIAL, true);
		StyleSetForeground(wxSTC_TEX_SYMBOL, wxColor(255, 0, 0)); // Operators
		StyleSetBackground(wxSTC_TEX_SYMBOL, wxColor(255, 255, 255));
		StyleSetBold(wxSTC_TEX_SYMBOL, false);
	}
	else if (filetype == FILE_DATAFILES)
	{
		this->SetLexer(wxSTC_LEX_OCTAVE);
		this->StyleSetForeground(wxSTC_MATLAB_COMMENT, wxColor(0, 128, 0));
		this->StyleSetItalic(wxSTC_MATLAB_COMMENT, false);
		this->StyleSetForeground(wxSTC_MATLAB_OPERATOR, wxColor(255, 0, 0));
		this->StyleSetBold(wxSTC_MATLAB_OPERATOR, false);
		this->StyleSetForeground(wxSTC_MATLAB_NUMBER, wxColor(0, 0, 128));
		this->StyleSetBackground(wxSTC_MATLAB_NUMBER, wxColor(255, 255, 255));
		this->StyleSetForeground(wxSTC_MATLAB_IDENTIFIER, wxColor(0, 0, 0));
		this->StyleSetBold(wxSTC_MATLAB_IDENTIFIER, false);
	}
	else if (filetype == FILE_MATLAB)
	{
		this->SetLexer(wxSTC_LEX_MATLAB);
		this->SetProperty("fold", "1");
		if (_syntax)
		{
			this->SetKeyWords(0, _syntax->getMatlab());
			this->SetKeyWords(1, _syntax->getMatlabFunctions());
		}

		for (int i = 0; i <= wxSTC_MATLAB_FUNCTIONS; i++)
		{
			SyntaxStyles _style;
			switch (i)
			{
				case wxSTC_MATLAB_DEFAULT:
				case wxSTC_MATLAB_IDENTIFIER:
					_style = m_options->GetSyntaxStyle(Options::STANDARD);
					break;
				case wxSTC_MATLAB_NUMBER:
					_style = m_options->GetSyntaxStyle(Options::NUMBER);
					break;
				case wxSTC_MATLAB_COMMENT:
					_style = m_options->GetSyntaxStyle(Options::COMMENT);
					break;
				case wxSTC_MATLAB_COMMAND:
				case wxSTC_MATLAB_KEYWORD:
					_style = m_options->GetSyntaxStyle(Options::COMMAND);
					break;
				case wxSTC_MATLAB_FUNCTIONS:
					_style = m_options->GetSyntaxStyle(Options::FUNCTION);
					break;
				case wxSTC_MATLAB_STRING:
				case wxSTC_MATLAB_DOUBLEQUOTESTRING:
					_style = m_options->GetSyntaxStyle(Options::STRING);
					break;
				case wxSTC_MATLAB_OPERATOR:
					_style = m_options->GetSyntaxStyle(Options::OPERATOR);
					break;
			}

			this->StyleSetForeground(i, _style.foreground);
			if (!_style.defaultbackground)
				this->StyleSetBackground(i, _style.background);
			else
				this->StyleSetBackground(i, this->StyleGetBackground(wxSTC_STYLE_DEFAULT));
			this->StyleSetBold(i, _style.bold);
			this->StyleSetItalic(i, _style.italics);
			this->StyleSetUnderline(i, _style.underline);
		}

	}
	else if (filetype == FILE_CPP)
	{
		this->SetLexer(wxSTC_LEX_CPP);
		this->SetProperty("fold", "1");
		if (_syntax)
		{
			this->SetKeyWords(0, _syntax->getCpp());
			this->SetKeyWords(1, _syntax->getCppFunctions());
		}

		for (int i = 0; i <= wxSTC_C_PREPROCESSORCOMMENT; i++)
		{
			SyntaxStyles _style;
			switch (i)
			{
				case wxSTC_C_DEFAULT :
				case wxSTC_C_IDENTIFIER:
					_style = m_options->GetSyntaxStyle(Options::STANDARD);
					break;
				case wxSTC_C_NUMBER:
					_style = m_options->GetSyntaxStyle(Options::NUMBER);
					break;
				case wxSTC_C_COMMENT:
				case wxSTC_C_COMMENTLINE:
					_style = m_options->GetSyntaxStyle(Options::COMMENT);
					break;
				case wxSTC_C_WORD:
					_style = m_options->GetSyntaxStyle(Options::COMMAND);
					break;
				case wxSTC_C_WORD2:
					_style = m_options->GetSyntaxStyle(Options::FUNCTION);
					break;
				case wxSTC_C_STRING:
					_style = m_options->GetSyntaxStyle(Options::STRING);
					break;
				case wxSTC_C_CHARACTER:
					_style = m_options->GetSyntaxStyle(Options::STRINGPARSER);
					break;
				case wxSTC_C_PREPROCESSOR:
					_style = m_options->GetSyntaxStyle(Options::INCLUDES);
					break;
				case wxSTC_C_OPERATOR:
					_style = m_options->GetSyntaxStyle(Options::OPERATOR);
					break;
			}

			this->StyleSetForeground(i, _style.foreground);
			if (!_style.defaultbackground)
				this->StyleSetBackground(i, _style.background);
			else
				this->StyleSetBackground(i, this->StyleGetBackground(wxSTC_STYLE_DEFAULT));
			this->StyleSetBold(i, _style.bold);
			this->StyleSetItalic(i, _style.italics);
			this->StyleSetUnderline(i, _style.underline);
		}
	}
	else if (filetype == FILE_DIFF)
    {
        SetLexer(wxSTC_LEX_DIFF);
        SetProperty("fold", "1");
        StyleSetForeground(wxSTC_DIFF_ADDED, wxColour(0, 128, 0));
        StyleSetBackground(wxSTC_DIFF_ADDED, wxColour(210, 255, 210));
        StyleSetForeground(wxSTC_DIFF_CHANGED, wxColour(128, 0, 0));
        StyleSetBackground(wxSTC_DIFF_CHANGED, wxColour(255, 210, 210));
        StyleSetForeground(wxSTC_DIFF_DELETED, wxColour(128, 0, 0));
        StyleSetBackground(wxSTC_DIFF_DELETED, wxColour(255, 210, 210));
        StyleSetForeground(wxSTC_DIFF_DEFAULT, *wxBLACK);
        StyleSetBackground(wxSTC_DIFF_DEFAULT, *wxWHITE);
        StyleSetForeground(wxSTC_DIFF_HEADER, *wxBLUE);
        StyleSetBackground(wxSTC_DIFF_HEADER, *wxWHITE);
        StyleSetForeground(wxSTC_DIFF_POSITION, wxColour(255, 128, 0));
        StyleSetBackground(wxSTC_DIFF_POSITION, *wxWHITE);
        StyleSetBold(wxSTC_DIFF_POSITION, true);
    }
	else
	{
		if (!getEditorSetting(SETTING_USETXTADV))
		{
			this->SetLexer(wxSTC_LEX_NULL);
			this->ClearDocumentStyle();
		}
		else
		{
			this->SetLexer(wxSTC_LEX_TXTADV);
			this->StyleSetItalic(wxSTC_TXTADV_DEFAULT, false);
			this->StyleSetItalic(wxSTC_TXTADV_MODIFIER, true);
			this->StyleSetForeground(wxSTC_TXTADV_MODIFIER, wxColor(255, 180, 180));
			this->StyleSetItalic(wxSTC_TXTADV_ITALIC, true);
			this->StyleSetItalic(wxSTC_TXTADV_BOLD, false);
			this->StyleSetBold(wxSTC_TXTADV_BOLD, true);
			this->StyleSetItalic(wxSTC_TXTADV_BOLD_ITALIC, true);
			this->StyleSetBold(wxSTC_TXTADV_BOLD_ITALIC, true);
			this->StyleSetUnderline(wxSTC_TXTADV_UNDERLINE, true);
			this->StyleSetForeground(wxSTC_TXTADV_STRIKETHROUGH, wxColor(140, 140, 140));
			this->StyleSetItalic(wxSTC_TXTADV_STRIKETHROUGH, true);
			this->StyleSetUnderline(wxSTC_TXTADV_URL, true);
			this->StyleSetForeground(wxSTC_TXTADV_URL, wxColor(0, 0, 255));
			this->StyleSetUnderline(wxSTC_TXTADV_HEAD, true);
			this->StyleSetBold(wxSTC_TXTADV_HEAD, true);
			this->StyleSetUnderline(wxSTC_TXTADV_BIGHEAD, true);
			this->StyleSetBold(wxSTC_TXTADV_BIGHEAD, true);
			this->StyleSetSize(wxSTC_TXTADV_BIGHEAD, this->StyleGetSize(0) + 1);
			this->StyleSetCase(wxSTC_TXTADV_BIGHEAD, wxSTC_CASE_UPPER);
		}
		//this->ClearDocumentStyle();
	}

	updateDefaultHighlightSettings();
	Colourise(0, -1);
	applyStrikeThrough();
	markLocalVariables(true);
}


/////////////////////////////////////////////////
/// \brief Performs all general default syntax
/// highlightings.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::updateDefaultHighlightSettings()
{
	this->CallTipSetForegroundHighlight(*wxBLUE);
	this->SetCaretLineVisible(true);
	this->SetIndentationGuides(wxSTC_IV_LOOKBOTH);

	this->StyleSetBackground(wxSTC_STYLE_INDENTGUIDE, m_options->GetSyntaxStyle(Options::STANDARD).foreground);
	this->StyleSetForeground(wxSTC_STYLE_INDENTGUIDE, m_options->GetSyntaxStyle(Options::STANDARD).background);

	this->SetCaretForeground(m_options->GetSyntaxStyle(Options::STANDARD).foreground);

	// Use these styles for enabling black mode
//	this->StyleSetBackground(wxSTC_STYLE_LINENUMBER, m_options->GetSyntaxStyle(Options::STANDARD).background);
//	this->StyleSetForeground(wxSTC_STYLE_LINENUMBER, m_options->GetSyntaxStyle(Options::STANDARD).foreground);
//
//	this->SetFoldMarginColour(true, m_options->GetSyntaxStyle(Options::STANDARD).background);
//	this->SetFoldMarginHiColour(true, m_options->GetSyntaxStyle(Options::STANDARD).foreground);

	if (!m_options->GetSyntaxStyle(Options::ACTIVE_LINE).defaultbackground)
		this->SetCaretLineBackground(m_options->GetSyntaxStyle(Options::ACTIVE_LINE).background);
	else
		this->SetCaretLineVisible(false);

	// standard settings for the brace highlighting
	this->StyleSetForeground(wxSTC_STYLE_BRACELIGHT, wxColour(0, 150, 0));
	this->StyleSetBackground(wxSTC_STYLE_BRACELIGHT, wxColour(0, 220, 0));
	this->StyleSetBold(wxSTC_STYLE_BRACELIGHT, true);
	this->StyleSetSize(wxSTC_STYLE_BRACELIGHT, this->StyleGetSize(0) + 1);
	this->StyleSetForeground(wxSTC_STYLE_BRACEBAD, wxColour(150, 0, 0));
	this->StyleSetBackground(wxSTC_STYLE_BRACEBAD, wxColour(220, 0, 0));
	this->StyleSetBold(wxSTC_STYLE_BRACEBAD, true);
	this->StyleSetSize(wxSTC_STYLE_BRACEBAD, this->StyleGetSize(0) + 1);

	// Style settings for the displayed annotations
	int nAnnotationFontSize = this->StyleGetSize(wxSTC_STYLE_DEFAULT);

	if (nAnnotationFontSize >= 10)
        nAnnotationFontSize -= 2;
    else if (nAnnotationFontSize >= 8)
        nAnnotationFontSize -= 1;

	this->StyleSetBackground(ANNOTATION_NOTE, wxColour(240, 240, 240));
	this->StyleSetForeground(ANNOTATION_NOTE, wxColour(120, 120, 120));
	this->StyleSetSize(ANNOTATION_NOTE, nAnnotationFontSize);
	this->StyleSetItalic(ANNOTATION_NOTE, true);
	this->StyleSetFaceName(ANNOTATION_NOTE, "Segoe UI");
	this->StyleSetBackground(ANNOTATION_WARN, wxColour(255, 255, 220));
	this->StyleSetForeground(ANNOTATION_WARN, wxColour(160, 160, 0));
	this->StyleSetSize(ANNOTATION_WARN, nAnnotationFontSize);
	this->StyleSetItalic(ANNOTATION_WARN, true);
	this->StyleSetFaceName(ANNOTATION_WARN, "Segoe UI");
	this->StyleSetBackground(ANNOTATION_ERROR, wxColour(255, 200, 200));
	this->StyleSetForeground(ANNOTATION_ERROR, wxColour(170, 0, 0));
	this->StyleSetSize(ANNOTATION_ERROR, nAnnotationFontSize);
	this->StyleSetItalic(ANNOTATION_ERROR, true);
	this->StyleSetFaceName(ANNOTATION_ERROR, "Segoe UI");
}


/////////////////////////////////////////////////
/// \brief Applies the strike-through effect.
///
/// \return void
///
/// This function is only used inf the advanced
/// text mode of the editor.
/////////////////////////////////////////////////
void NumeReEditor::applyStrikeThrough()
{
	if (!getEditorSetting(SETTING_USETXTADV)
			|| m_fileType == FILE_NSCR
			|| m_fileType == FILE_NPRC
			|| m_fileType == FILE_TEXSOURCE
			|| m_fileType == FILE_DATAFILES)
		return;

	SetIndicatorCurrent(HIGHLIGHT_STRIKETHROUGH);
	IndicatorClearRange(0, GetLastPosition());
	IndicatorSetStyle(HIGHLIGHT_STRIKETHROUGH, wxSTC_INDIC_STRIKE);
	IndicatorSetForeground(HIGHLIGHT_STRIKETHROUGH, wxColor(255, 0, 0));

	for (int i = 0; i < GetLastPosition(); i++)
	{
		if (GetStyleAt(i) == wxSTC_TXTADV_STRIKETHROUGH)
		{
			for (int j = i; j < GetLastPosition(); j++)
			{
				if (GetStyleAt(j) == wxSTC_TXTADV_MODIFIER || j == GetLastPosition() - 1)
				{
					IndicatorFillRange(i, j - i);
					i = j;
					break;
				}
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////
///  public SetFilename
///  Sets the filename for the editor
///
///  @param  filename     wxFileName  The filename for this editor
///  @param  fileIsRemote bool        Whether this file is remote or local
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::SetFilename(wxFileName filename, bool fileIsRemote)
{
	m_bLastSavedRemotely = fileIsRemote;

	//m_fileNameAndPath.Assign(path, name, fileIsRemote ? wxPATH_UNIX : wxPATH_DOS);

	if (m_project->IsSingleFile())
	{
		wxString oldFileName = m_fileNameAndPath.GetFullPath(m_bLastSavedRemotely ? wxPATH_UNIX : wxPATH_DOS);

		if (m_project->FileExistsInProject(oldFileName))
		{
			FileFilterType oldFilterType;
			wxString oldExtension = m_fileNameAndPath.GetExt();
			if (oldExtension.StartsWith("h"))
			{
				oldFilterType = FILE_NPRC;
			}
			else if (oldExtension.StartsWith("c"))
			{
				oldFilterType = FILE_NSCR;
			}
			else if (oldExtension.StartsWith("txt"))
			{
				oldFilterType = FILE_NONSOURCE;
			}
			else
			{
				oldFilterType = FILE_NONSOURCE;
			}

			m_project->RemoveFileFromProject(oldFileName, oldFilterType);
		}

		wxString newFileName = filename.GetFullPath(fileIsRemote ? wxPATH_UNIX : wxPATH_DOS);
		if (!m_project->FileExistsInProject(newFileName))
		{
			FileFilterType newFilterType;
			wxString newExtension = filename.GetExt();
			if (newExtension.StartsWith("h"))
			{
				newFilterType = FILE_NPRC;
			}
			else if (newExtension.StartsWith("c"))
			{
				newFilterType = FILE_NSCR;
			}
			else if (newExtension.StartsWith("txt"))
			{
				newFilterType = FILE_NONSOURCE;
			}
			else
			{
				newFilterType = FILE_NONSOURCE;
			}
			m_project->AddFileToProject(newFileName, newFilterType);
		}

		m_project->SetRemote(fileIsRemote);
	}

	m_fileNameAndPath = filename;
}


//////////////////////////////////////////////////////////////////////////////
///  public GetFileNameAndPath
///  Gets the full pathname of this file as a string
///
///  @return wxString The full pathname of this file
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
wxString NumeReEditor::GetFileNameAndPath()
{
	wxString nameAndPath = m_fileNameAndPath.GetFullPath();//m_bLastSavedRemotely ? wxPATH_UNIX : wxPATH_DOS);
	return nameAndPath;
}


//////////////////////////////////////////////////////////////////////////////
///  public GetFilenameString
///  Gets the name of this file with no path
///
///  @return wxString The name of this file
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
wxString NumeReEditor::GetFilenameString()
{
	return m_fileNameAndPath.GetFullName();
}


//////////////////////////////////////////////////////////////////////////////
///  public GetFileName
///  Gets the wxFileName for this file
///
///  @return wxFileName The editor's filename
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
wxFileName NumeReEditor::GetFileName()
{
	return m_fileNameAndPath;
}


//////////////////////////////////////////////////////////////////////////////
///  public GetFilePath
///  Gets the path for this file
///
///  @return wxString The path for this file
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
wxString NumeReEditor::GetFilePath()
{
	return m_fileNameAndPath.GetPath(false, m_bLastSavedRemotely ? wxPATH_UNIX : wxPATH_DOS);
}


//////////////////////////////////////////////////////////////////////////////
///  public ResetEditor
///  Clears out the editor's contents and resets it completely
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::ResetEditor()
{
	ClearAll();

	m_fileNameAndPath.Clear();
	m_breakpoints.Clear();
	m_clickedWord.clear();
	m_clickedProcedure.clear();
	m_clickedInclude.clear();
	m_clickedWordLength = 0;
	m_watchedString.clear();
	vRenameSymbolsChangeLog.clear();

	SetReadOnly(false);
	SetText(wxEmptyString);
	SetSavePoint();
	EmptyUndoBuffer();

	MarkerDeleteAll(MARKER_BREAKPOINT);
	MarkerDeleteAll(MARKER_FOCUSEDLINE);
	MarkerDeleteAll(MARKER_MODIFIED);
	MarkerDeleteAll(MARKER_SAVED);

	if (m_project != NULL && m_project->IsSingleFile())
	{
		delete m_project;
	}

	m_project = new ProjectInfo();
}


//////////////////////////////////////////////////////////////////////////////
///  public OnRightClick
///  Handles a right-click in the editor
///
///  @param  event wxMouseEvent & The generated mouse event
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::OnRightClick(wxMouseEvent& event)
{
	m_PopUpActive = true;
	m_lastRightClick = event.GetPosition();
	int charpos = PositionFromPoint(m_lastRightClick);
	int linenum = LineFromPosition(charpos);
	const int nINSERTIONPOINT = 16;
    wxString clickedWord = m_search->FindClickedWord();

	// Determine the marker and breakpoint conditions
	bool breakpointOnLine = MarkerOnLine(linenum, MARKER_BREAKPOINT);
	bool bookmarkOnLine = MarkerOnLine(linenum, MARKER_BOOKMARK);
	bool breakpointsAllowed = isNumeReFileType();

	// returns a copy of a member variable, which would seem sort of pointless, but
	// GetBreakpoints cleans up any stray marker IDs in the list before returning
	// so we have an accurate count of how many breakpoints there are
	wxArrayInt currentBreakpoints = GetBreakpoints();
	bool canClearBreakpoints = currentBreakpoints.GetCount() > 0;

	// Prepare the context menu
	if (m_popupMenu.FindItem(ID_DEBUG_DISPLAY_SELECTION) != nullptr)
	{
		m_popupMenu.Enable(ID_DEBUG_DISPLAY_SELECTION, false);
		m_menuShowValue->SetItemLabel(_guilang.get("GUI_MENU_EDITOR_HIGHLIGHT", "..."));
	}

	if (m_popupMenu.FindItem(ID_FIND_PROCEDURE) != nullptr)
	{
		m_popupMenu.Remove(ID_FIND_PROCEDURE);
	}

	if (m_popupMenu.FindItem(ID_FIND_INCLUDE) != nullptr)
	{
		m_popupMenu.Remove(ID_FIND_INCLUDE);
	}

	if (m_popupMenu.FindItem(ID_MENU_HELP_ON_ITEM) != nullptr)
	{
		m_popupMenu.Remove(ID_MENU_HELP_ON_ITEM);
	}

	if (m_popupMenu.FindItem(ID_REFACTORING_MENU) != nullptr)
	{
	    m_refactoringMenu->Enable(ID_RENAME_SYMBOLS, false);
	    m_refactoringMenu->Enable(ID_ABSTRAHIZE_SECTION, false);
		m_popupMenu.Remove(ID_REFACTORING_MENU);
	}

	// Enable menus depending on the marker and breakpint states
	m_popupMenu.Enable(ID_FOLD_CURRENT_BLOCK, isCodeFile());
	m_popupMenu.Enable(ID_HIDE_SELECTION, HasSelection());
	m_popupMenu.Enable(ID_DEBUG_ADD_BREAKPOINT, breakpointsAllowed && !breakpointOnLine);
	m_popupMenu.Enable(ID_DEBUG_REMOVE_BREAKPOINT, breakpointsAllowed && breakpointOnLine);
	m_popupMenu.Enable(ID_BOOKMARK_ADD, !bookmarkOnLine);
	m_popupMenu.Enable(ID_BOOKMARK_REMOVE, bookmarkOnLine);
	m_popupMenu.Enable(ID_DEBUG_CLEAR_ALL_BREAKPOINTS, canClearBreakpoints);

	// Enable upper- and lowercase if the user made
	// a selection in advance
	if (HasSelection())
	{
		m_popupMenu.Enable(ID_UPPERCASE, true);
		m_popupMenu.Enable(ID_LOWERCASE, true);
	}
	else
	{
		m_popupMenu.Enable(ID_UPPERCASE, false);
		m_popupMenu.Enable(ID_LOWERCASE, false);
	}

    // If th user clicked a word or made a selection
    if (clickedWord.Length() > 0 || HasSelection())
    {
        if (this->GetStyleAt(charpos) == wxSTC_NSCR_PROCEDURES)
        {
            // Show "find procedure"
            wxString clickedProc = m_search->FindClickedProcedure();

            if (clickedProc.length())
            {
                m_popupMenu.Insert(nINSERTIONPOINT, m_menuFindProcedure);
                m_menuFindProcedure->SetItemLabel(_guilang.get("GUI_MENU_EDITOR_FINDPROC", clickedProc.ToStdString()));
            }
        }
        else if (this->GetStyleAt(charpos) == wxSTC_NSCR_COMMAND
                || this->GetStyleAt(charpos) == wxSTC_NSCR_PROCEDURE_COMMANDS
                || this->GetStyleAt(charpos) == wxSTC_NSCR_OPTION)
        {
            // Show "help on item"
            m_popupMenu.Insert(nINSERTIONPOINT, m_menuHelpOnSelection);
            m_menuHelpOnSelection->SetItemLabel(_guilang.get("GUI_TREE_PUP_HELPONITEM", clickedWord.ToStdString()));
        }
        else if (this->GetStyleAt(charpos) == wxSTC_NSCR_INCLUDES
                || this->GetStyleAt(charpos) == wxSTC_NPRC_INCLUDES)
        {
            // Show "find included file"
            wxString clickedInclude = m_search->FindClickedInclude();

            if (clickedInclude.length())
            {
                m_popupMenu.Insert(nINSERTIONPOINT, m_menuFindInclude);
                m_menuFindInclude->SetItemLabel(_guilang.get("GUI_MENU_EDITOR_FINDINCLUDE", clickedInclude.ToStdString()));
            }
        }
        else
        {
            // Show the refactoring menu
            m_popupMenu.Insert(nINSERTIONPOINT, m_menuRefactoring);

            if (this->isStyleType(STYLE_DEFAULT, charpos) || this->isStyleType(STYLE_IDENTIFIER, charpos) || this->isStyleType(STYLE_DATAOBJECT, charpos) || this->isStyleType(STYLE_FUNCTION, charpos))
                m_refactoringMenu->Enable(ID_RENAME_SYMBOLS, true);

            if (HasSelection())
                m_refactoringMenu->Enable(ID_ABSTRAHIZE_SECTION, true);
        }

        // Enable the "highlight" menu item and add the clicked word
        // to the item text
        m_popupMenu.Enable(ID_DEBUG_DISPLAY_SELECTION, true);
        m_menuShowValue->SetItemLabel(_guilang.get("GUI_MENU_EDITOR_HIGHLIGHT", clickedWord.ToStdString()));

        // Set the boolean flag to correspond to the highlight
        // state of the clicked word
        if (m_clickedWord == m_watchedString && m_clickedWord.length())
            m_menuShowValue->Check(true);
        else
            m_menuShowValue->Check(false);
    }

    // Cancel the call tip, if there's one active
	if (this->CallTipActive())
		this->AdvCallTipCancel();

	PopupMenu(&m_popupMenu, m_lastRightClick);
	m_PopUpActive = false;
}


/////////////////////////////////////////////////
/// \brief Marks the selected line as modified.
///
/// \param nLine int
/// \return void
///
/// The modified bar on the left margin will be
/// shown in yellow.
/////////////////////////////////////////////////
void NumeReEditor::markModified(int nLine)
{
	int nMarkSaved = 1 << (MARKER_SAVED);
	int nMarkModified = 1 << (MARKER_MODIFIED);

	while (this->MarkerGet(nLine) & nMarkSaved)
		this->MarkerDelete(nLine, MARKER_SAVED);

	if (!(this->MarkerGet(nLine) & nMarkModified))
		this->MarkerAdd(nLine, MARKER_MODIFIED);
}


/////////////////////////////////////////////////
/// \brief Marks the complete document as saved.
///
/// \return void
///
/// The modified bar on the left margin will be
/// shown on green, where it had been shown in
/// yellow before.
/////////////////////////////////////////////////
void NumeReEditor::markSaved()
{
	int nMarkModified = 1 << (MARKER_MODIFIED);
	int nMarkSaved = 1 << (MARKER_SAVED);
	int nNextLine = 0;

	while ((nNextLine = this->MarkerNext(0, nMarkModified)) != -1)
	{
		this->MarkerDelete(nNextLine, MARKER_MODIFIED);

		if (!(this->MarkerGet(nNextLine) & nMarkSaved))
			this->MarkerAdd(nNextLine, MARKER_SAVED);
	}

	this->MarkerDeleteAll(MARKER_MODIFIED);
}


//////////////////////////////////////////////////////////////////////////////
///  private OnEditorModified
///  Updates the editor's project when the editor is modified
///
///  @param  event wxStyledTextEvent & The generated editor event
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::OnEditorModified(wxStyledTextEvent& event)
{
	m_project->SetCompiled(false);

	if (!m_bLoadingFile && (event.GetModificationType() & wxSTC_MOD_INSERTTEXT || event.GetModificationType() & wxSTC_MOD_DELETETEXT))
	{
		m_modificationHappened = true;
		int nLine = this->LineFromPosition(event.GetPosition());
		int nLinesAdded = event.GetLinesAdded();

		if (nLinesAdded > 0)
		{
			for (int i = 0; i < nLinesAdded; i++)
				this->markModified(i + nLine);
		}
		else if (nLinesAdded < 0)
			this->markModified(nLine);
		else
			this->markModified(nLine);
	}

	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Folds the block, to which the current
/// line belongs.
///
/// \param nLine int
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::FoldCurrentBlock(int nLine)
{
	// Get parent line
	int nParentline = this->GetFoldParent(nLine);

	// Probably the current line is also a parent line -> take this one
	if (this->GetFoldLevel(nLine) & wxSTC_FOLDLEVELHEADERFLAG)
		nParentline = nLine;

	// if not found -> return
	if (nParentline == -1)
		return;

	// if already folded -> return
	if (!this->GetFoldExpanded(nLine))
		return;

	// toggle the fold state of the current header line -> only folds because of previous condition
	this->ToggleFold(nParentline);
}


/////////////////////////////////////////////////
/// \brief Adds markers to section headlines.
///
/// \param bForceRefresh bool \c true to clear
/// every marker first
/// \return void
///
/// Adds markers on the sidebar at the headline of
/// every section of NumeRe, MATLAB, LaTeX and text
/// files with advanced text highlighting active.
/// The markers are used as automatic bookmarks,
/// so the user may jump to the markers using the
/// same short key.
/////////////////////////////////////////////////
void NumeReEditor::markSections(bool bForceRefresh)
{
    // Ensure, that sectioning is activated
	if (!getEditorSetting(SETTING_USESECTIONS))
		return;

    if (bForceRefresh)
        this->MarkerDeleteAll(MARKER_SECTION);

    int startline = 0;
    int endline = this->GetLineCount();

    // Find the first and last line, if not the whole
    // editor shall be examined
    if (!bForceRefresh)
    {
        int markermask = (1 << MARKER_SECTION);

        if ((startline = this->MarkerPrevious(this->GetCurrentLine() - 1, markermask)) == -1)
            startline = 0;

        if ((endline = this->MarkerNext(this->GetCurrentLine() + 1, markermask)) == -1)
            endline = this->GetLineCount();
    }

    // NumeRe or MATLAB file?
	if (m_fileType == FILE_NSCR || m_fileType == FILE_NPRC || m_fileType == FILE_MATLAB)
	{
		// Search for documentation comment blocks
		// in the file and add markers
		for (int i = startline; i < endline; i++)
		{
			if (i && this->MarkerOnLine(i - 1, MARKER_SECTION))
				continue;

			for (int j = this->PositionFromLine(i); j < this->GetLineEndPosition(i) + 1; j++)
			{
				if (this->GetCharAt(j) == ' ' || this->GetCharAt(j) == '\t')
					continue;

				if (isStyleType(STYLE_COMMENT_SECTION_LINE, j)
						|| isStyleType(STYLE_COMMENT_SECTION_BLOCK, j))
				{
					if (!this->MarkerOnLine(i, MARKER_SECTION))
						this->MarkerAdd(i, MARKER_SECTION);

					break;
				}

				// only section markers which are the first characters in line are interpreted
				if (this->GetCharAt(j) != ' ' && this->GetCharAt(j) != '\t')
				{
					if (this->MarkerOnLine(i, MARKER_SECTION))
						this->MarkerDelete(i, MARKER_SECTION);

					break;
				}
			}
		}
	}

	// LaTeX source?
	if (m_fileType == FILE_TEXSOURCE)
	{
		// Search for LaTeX sectioning commands in
		// the examination range and add markers to them
		for (int i = startline; i < endline; i++)
		{
			for (int j = this->PositionFromLine(i); j < this->GetLineEndPosition(i) + 1; j++)
			{
				if (this->GetCharAt(j) == ' ' || this->GetCharAt(j) == '\t')
					continue;

				if (this->GetStyleAt(j) == wxSTC_TEX_COMMAND)
				{
					int wordstart = this->WordStartPosition(j, false);
					int wordend = this->WordEndPosition(j, false);

					wxString word = this->GetTextRange(wordstart, wordend);

					if (word == "maketitle"
							|| word == "part"
							|| word == "chapter"
							|| word == "section"
							|| word == "subsection"
							|| word == "subsubsection"
							|| word == "subsubsubsection"
							|| word == "paragraph"
							|| word == "subparagraph"
							|| word == "addchap"
							|| word == "addsec")
					{
						if (!this->MarkerOnLine(i, MARKER_SECTION))
							this->MarkerAdd(i, MARKER_SECTION);
					}

					j = wordend;
				}
			}
		}
	}

	// Text file with advanced highlighting?
	if (m_fileType == FILE_NONSOURCE && this->getEditorSetting(SETTING_USETXTADV))
	{
		// Search for all headlines in the document and
		// add markers to them
		for (int i = startline; i < endline; i++)
		{
			for (int j = this->PositionFromLine(i); j < this->GetLineEndPosition(i) + 1; j++)
			{
				if (this->GetCharAt(j) == ' ' || this->GetCharAt(j) == '\t')
					continue;

				if (this->GetStyleAt(j) == wxSTC_TXTADV_BIGHEAD || this->GetStyleAt(j) == wxSTC_TXTADV_HEAD)
				{
					if (!this->MarkerOnLine(i, MARKER_SECTION))
						this->MarkerAdd(i, MARKER_SECTION);

					break;
				}
			}

			if (this->GetLine(i).find('#') == string::npos)
			{
				if (this->MarkerOnLine(i, MARKER_SECTION))
					this->MarkerDelete(i, MARKER_SECTION);
			}
		}
	}
}


/////////////////////////////////////////////////
/// \brief This method wraps the detection of
/// local variables.
///
/// \param bForceRefresh bool
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::markLocalVariables(bool bForceRefresh)
{
    if (m_fileType != FILE_NPRC || !m_options->GetHighlightLocalVariables())
        return;

    SetIndicatorCurrent(HIGHLIGHT_LOCALVARIABLES);

    // We clean everything, if need to refresh the indicators
    if (bForceRefresh)
        IndicatorClearRange(0, GetLastPosition());
    else if (GetStyleAt(GetLineIndentPosition(GetCurrentLine())) != wxSTC_NPRC_COMMAND)
        return;

	IndicatorSetStyle(HIGHLIGHT_LOCALVARIABLES, wxSTC_INDIC_DOTS);
	IndicatorSetForeground(HIGHLIGHT_LOCALVARIABLES, *wxBLACK);

	// Run the algorithm for every possible local variable declarator
	markLocalVariableOfType("var", bForceRefresh);
	markLocalVariableOfType("cst", bForceRefresh);
	markLocalVariableOfType("tab", bForceRefresh);
	markLocalVariableOfType("str", bForceRefresh);
}


/////////////////////////////////////////////////
/// \brief This method finds all local variables
/// of the selected type and highlights their
/// definitions.
///
/// \param command const wxString&
/// \param bForceRefresh bool
/// \return void
///
/// If the refresh flag is not set, then the
/// alogrithm will only update the current edited
/// line of the editor.
/////////////////////////////////////////////////
void NumeReEditor::markLocalVariableOfType(const wxString& command, bool bForceRefresh)
{
    vector<int> matches;

    // Find the occurences of the variable declaration commands
    // in the corresponding scope
    if (!bForceRefresh)
    {
        matches = m_search->FindAll(command, wxSTC_NPRC_COMMAND, PositionFromLine(GetCurrentLine()), GetLineEndPosition(GetCurrentLine()), false);

        if (matches.size())
            IndicatorClearRange(PositionFromLine(GetCurrentLine()), GetLineEndPosition(GetCurrentLine()));
    }
    else
        matches = m_search->FindAll(command, wxSTC_NPRC_COMMAND, 0, GetLastPosition(), false);

    // Run through all found occurences and extract the definitions
    // of the local variables
    for (size_t i = 0; i < matches.size(); i++)
    {
        wxString line = GetTextRange(matches[i]+command.length(), GetLineEndPosition(LineFromPosition(matches[i])));
        int nPos = line.find_first_not_of(' ') + matches[i] + command.length();

        for (int j = nPos; j < GetLineEndPosition(LineFromPosition(matches[i])) + 1; j++)
        {
            char currentChar = GetCharAt(j);

            // If a separator character was found, highlight
            // the current word and find the next candidate
            if (currentChar == ' ' || currentChar == '=' || currentChar == ',' || currentChar == '\r' || currentChar == '\n')
            {
                IndicatorFillRange(nPos, j - nPos);

                if (currentChar == ',')
                {
                    // Only a declaration, find the next one
                    while (GetCharAt(j) == ',' || GetCharAt(j) == ' ')
                        j++;
                }
                else
                {
                    // This is also an initialization. Find the
                    // next declaration by jumping over the assigned
                    // value.
                    for (int l = j; l < GetLineEndPosition(LineFromPosition(matches[i])); l++)
                    {
                        if (GetCharAt(l) == ',' && GetStyleAt(l) == wxSTC_NPRC_OPERATORS)
                        {
                            while (GetCharAt(l) == ',' || GetCharAt(l) == ' ')
                                l++;

                            j = l;
                            break;
                        }
                        else if (GetStyleAt(l) == wxSTC_NPRC_OPERATORS && (GetCharAt(l) == '(' || GetCharAt(l) == '{'))
                        {
                            j = -1;
                            l = BraceMatch(l);

                            if (l == -1)
                                break;
                        }
                        else if (l+1 == GetLineEndPosition(LineFromPosition(matches[i])))
                            j = -1;
                    }
                }

                if (j == -1)
                    break;

                nPos = j;
            }
            else if (GetStyleAt(j) == wxSTC_NPRC_OPERATORS && (currentChar == '(' || currentChar == '{'))
            {
                j = BraceMatch(j);

                if (j == -1)
                    break;
            }
        }
    }
}


/////////////////////////////////////////////////
/// \brief Performs asynchronous actions and is
/// called automatically.
///
/// \return void
///
/// The asynchronous actions include the following
/// items:
/// \li Automatic code indentation
/// \li Display and update of function call tip
/////////////////////////////////////////////////
void NumeReEditor::AsynchActions()
{
	if (!this->AutoCompActive() && this->getEditorSetting(SETTING_INDENTONTYPE) && (m_fileType == FILE_NSCR || m_fileType == FILE_NPRC || m_fileType == FILE_MATLAB || m_fileType == FILE_CPP))
		ApplyAutoIndentation(0, this->GetCurrentLine() + 1);

	HandleFunctionCallTip();
}


/////////////////////////////////////////////////
/// \brief Performs asynchronous evaluations and
/// is called automatically.
///
/// \return void
///
/// The asynchronous evaluations include the
/// following items:
/// \li static code analysis
/// \li update of sectioning markers
/////////////////////////////////////////////////
void NumeReEditor::AsynchEvaluations()
{
	if (getEditorSetting(SETTING_USEANALYZER))
		m_analyzer->run();

	markSections();
	markLocalVariables();
}


/////////////////////////////////////////////////
/// \brief Event handler for starting drag 'n drop.
///
/// \param event wxStyledTextEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnStartDrag(wxStyledTextEvent& event)
{
	wxString gtxt = event.GetDragText();
}


/////////////////////////////////////////////////
/// \brief Event handler for stopping drag 'n drop.
///
/// \param event wxStyledTextEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnDragOver(wxStyledTextEvent& event)
{
	event.SetDragResult(wxDragMove);
	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Event handler for stopping drag 'n drop.
/// (Actually does nothing)
///
/// \param event wxStyledTextEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnDrop(wxStyledTextEvent& event)
{
	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Event handler for moving while performing
/// a drag 'n drop operation.
///
/// \param event wxStyledTextEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnMouseMotion(wxMouseEvent& event)
{
	if (m_dragging)
		DoDragOver(event.GetX(), event.GetY(), wxDragMove);

	event.Skip();
}


/////////////////////////////////////////////////
/// \brief Jumps to the predefined template caret
/// position.
///
/// \param nStartPos int
/// \return void
///
/// This member function jumps the caret to the predefined
/// caret position (using a pipe "|") in the template and
/// removes the character at the position
/////////////////////////////////////////////////
void NumeReEditor::GotoPipe(int nStartPos)
{
    vector<int> vPos;

    // Try to find the pipe in the file
    if (m_fileType == FILE_NSCR || m_fileType == FILE_NPRC)
        vPos = m_search->FindAll("|", wxSTC_NSCR_OPERATORS, nStartPos, GetLastPosition(), false);

    // If nothting was found, try to find the pipe
    // in the install section
    if (m_fileType == FILE_NSCR && !vPos.size())
        vPos = m_search->FindAll("|", wxSTC_NSCR_INSTALL, nStartPos, GetLastPosition(), false);

    // Go to the pipe, if it was found, and remove
    // it
    if (vPos.size())
    {
        GotoPos(vPos.front());
        DeleteRange(vPos.front(), 1);
    }
    else
        GotoLine(nStartPos); // fallback solution
}


//////////////////////////////////////////////////////////////////////////////
///  private OnAddBreakpoint
///  Adds a breakpoint to this file
///
///  @param  event wxCommandEvent & The generated menu event
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::OnAddBreakpoint(wxCommandEvent& event)
{
	int linenum = GetLineForMarkerOperation();
	AddBreakpoint(linenum);
	ResetRightClickLocation();
}


//////////////////////////////////////////////////////////////////////////////
///  private OnRemoveBreakpoint
///  Removes a breakpoint from this file
///
///  @param  event wxCommandEvent & The generated menu event
///
///  @return void
///
///  @remarks This doesn't clean out the marker handle that STC gives us,
///  @remarks since there's no way to check what marker handles are on a given line.
///  @remarks Orphaned marker handles are cleaned up in GetBreakpoints.
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::OnRemoveBreakpoint(wxCommandEvent& event)
{
	int linenum = GetLineForMarkerOperation();
	RemoveBreakpoint(linenum);
	ResetRightClickLocation();
}


//////////////////////////////////////////////////////////////////////////////
///  private OnClearBreakpoints
///  Clears all breakpoints from this file
///
///  @param  event wxCommandEvent & The generated menu event
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::OnClearBreakpoints(wxCommandEvent& event)
{
	// m_breakpoints should have been cleared of any orphaned marker
	// handles during the right-click that led us here
	int numBreakpoints = GetBreakpoints().GetCount();

	for (int i = 0; i < numBreakpoints; i++)
	{
		int markerHandle = m_breakpoints[i];
		int linenum = this->MarkerLineFromHandle(markerHandle);
		this->MarkerDeleteHandle(markerHandle);
		CreateBreakpointEvent(linenum, false);
	}

	m_terminal->clearBreakpoints(GetFileNameAndPath().ToStdString());
	ResetRightClickLocation();
}


/////////////////////////////////////////////////
/// \brief Adds a bookmark at the selected line.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnAddBookmark(wxCommandEvent& event)
{
	int nLineNumber = GetLineForMarkerOperation();
	this->MarkerAdd(nLineNumber, MARKER_BOOKMARK);
	ResetRightClickLocation();
}


/////////////////////////////////////////////////
/// \brief Removes a bookmark from the selected line.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnRemoveBookmark(wxCommandEvent& event)
{
	int nLineNumber = GetLineForMarkerOperation();
	this->MarkerDelete(nLineNumber, MARKER_BOOKMARK);
}


/////////////////////////////////////////////////
/// \brief Removes all bookmarks from the whole
/// file.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnClearBookmarks(wxCommandEvent& event)
{
	this->MarkerDeleteAll(MARKER_BOOKMARK);
}


//////////////////////////////////////////////////////////////////////////////
///  public GetBreakpoints
///  Gets a list of all breakpoint line numbers.  Also clears out any invalid (removed) breakpoint IDs.
///
///  @return wxArrayInt The line numbers for all the breakpoints in this file
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
wxArrayInt NumeReEditor::GetBreakpoints()
{
	wxArrayInt linenumbers;
	wxArrayInt invalidBreakpoints;

	int numStoredBreakpoints = m_breakpoints.GetCount();
	for (int i = 0; i < numStoredBreakpoints; i++)
	{
		int markerHandle = m_breakpoints[i];

		int linenum = this->MarkerLineFromHandle(markerHandle);

		if (linenum != -1)
		{
			linenumbers.Add(linenum + 1);
		}
		else
		{
			invalidBreakpoints.Add(markerHandle);
		}
	}

	for (int i = 0; i < (int)invalidBreakpoints.GetCount(); i++)
	{
		m_breakpoints.Remove(invalidBreakpoints[i]);
	}

	linenumbers.Sort((CMPFUNC_wxArraywxArrayInt)CompareInts);
	return linenumbers;
}


//////////////////////////////////////////////////////////////////////////////
///  public HasBeenCompiled
///  Returns the compiled status for this editor's project
///
///  @return bool Whether or not the editor's project has been compiled
///
///  @author Mark Erikson @date 04-22-2004
///
/// \todo Evaluate, whether this method is still needed
//////////////////////////////////////////////////////////////////////////////
bool NumeReEditor::HasBeenCompiled()
{
	return m_project->IsCompiled();
}


//////////////////////////////////////////////////////////////////////////////
///  public SetCompiled
///  Set this editor's project's compiled status
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
///
/// \todo Evaluate, whether this method is still needed
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::SetCompiled()
{
	m_project->SetCompiled(true);
}


/////////////////////////////////////////////////
/// \brief Registers the passed procedure viewer.
///
/// \param viewer ProcedureViewer*
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::registerProcedureViewer(ProcedureViewer* viewer)
{
    m_procedureViewer = viewer;
}


//////////////////////////////////////////////////////////////////////////////
///  public FocusOnLine
///  Moves the cursor to the given line number, optionally showing a highlight marker
///
///  @param  linenumber int   The line to go to
///  @param  showMarker bool  [=true] Whether or not to mark the line
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::FocusOnLine(int linenumber, bool showMarker)
{
	GotoLine(linenumber);
    SetFirstVisibleLine(VisibleFromDocLine(linenumber) - m_options->GetDebuggerFocusLine());
    EnsureVisible(linenumber);

	// Unhide the lines, if the current line is part
	// of a hidden sectioon
	if (!GetLineVisible(linenumber))
    {
        int nFirstLine = linenumber-1;
        int nLastLine = linenumber+1;

        // Find the first unhidden line
        while (!GetLineVisible(nFirstLine))
            nFirstLine--;

        // Find the last hidden line
        while (!GetLineVisible(nLastLine))
            nLastLine++;

        // Show the lines
        ShowLines(nFirstLine, nLastLine);

        // Delete the markers
        for (int i = nFirstLine; i < nLastLine; i++)
        {
            MarkerDelete(i, MARKER_HIDDEN);
            MarkerDelete(i, MARKER_HIDDEN_MARGIN);
        }
    }

	if (showMarker)
	{
		MarkerDeleteAll(MARKER_FOCUSEDLINE);
		MarkerAdd(linenumber, MARKER_FOCUSEDLINE);
	}
}


//////////////////////////////////////////////////////////////////////////////
///  private CreateBreakpointEvent
///  Sets up a debug event when a breakpoint is added or deleted
///
///  @param  linenumber    int   The line number of the toggled breakpoint
///  @param  addBreakpoint bool  Whether the breakpoint is being added or deleted
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
///
/// \todo Evaluate, whether this method is still needed
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::CreateBreakpointEvent(int linenumber, bool addBreakpoint)
{
	wxDebugEvent dbg;
	wxString filename = m_fileNameAndPath.GetFullPath(wxPATH_UNIX);
	dbg.SetSourceFilename(filename);

	// adjust for the zero-based index
	dbg.SetLineNumber(linenumber + 1);
	int type = addBreakpoint ? ID_DEBUG_ADD_BREAKPOINT : ID_DEBUG_REMOVE_BREAKPOINT;
	dbg.SetId(type);
	dbg.SetId(type);
	dbg.SetProject(m_project);
}


//////////////////////////////////////////////////////////////////////////////
///  public SetProject
///  Sets the project for this editor
///
///  @param  project ProjectInfo * The new project
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
///
/// \todo Evaluate, whether this method is still needed
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::SetProject(ProjectInfo* project)
{
	if (m_project != NULL && m_project->IsSingleFile())
	{
		delete m_project;
	}

	m_project = project;
	m_project->AddEditor(this);
}


//////////////////////////////////////////////////////////////////////////////
///  private OnRunToCursor
///  Creates a "one-shot" breakpoint and tells the debugger to run to that line
///
///  @param  event wxCommandEvent & The generated menu event
///
///  @return void
///
///  @author Mark Erikson @date 04-22-2004
///
/// \todo Evaluate, whether this method is still needed
//////////////////////////////////////////////////////////////////////////////
void NumeReEditor::OnRunToCursor(wxCommandEvent& event)
{
	/*
	int charpos = PositionFromPoint(m_lastRightClick);
	int linenum = LineFromPosition(charpos);
	// adjust for Scintilla's internal zero-based line numbering
	linenum++;
	*/

	/*int linenum = GetLineForBreakpointOperation();
	wxDebugEvent debugEvent;
	debugEvent.SetId(ID_DEBUG_RUNTOCURSOR);
	debugEvent.SetSourceFilename(GetFilenameString());
	debugEvent.SetLineNumber(linenum);
	m_debugManager->AddPendingEvent(debugEvent);//m_mainFrame->AddPendingEvent(debugEvent);*/

	ResetRightClickLocation();
}


/////////////////////////////////////////////////
/// \brief Gets the line belonging to the last
/// right mouse click or the current line of the
/// caret.
///
/// \return int
///
/////////////////////////////////////////////////
int NumeReEditor::GetLineForMarkerOperation()
{
	int lineNum = 0;

	if (m_lastRightClick.x < 0 || m_lastRightClick.y < 0)
		lineNum =  GetCurrentLine();
	else
	{
		int charpos = PositionFromPoint(m_lastRightClick);
		lineNum = LineFromPosition(charpos);
	}

	return lineNum;
}


/////////////////////////////////////////////////
/// \brief Invalidates the latest mouse right click
/// position.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::ResetRightClickLocation()
{
	m_lastRightClick.x = -1;
	m_lastRightClick.y = -1;
}


/////////////////////////////////////////////////
/// \brief Wrapper for the static code analyzer.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::AnalyseCode()
{
    m_analyzer->run();
}


/////////////////////////////////////////////////
/// \brief Finds the procedure definition and displays
/// it in the editor.
///
/// \param procedurename const wxString&
/// \return void
///
/// This private member function searches for the procedure
/// definition in the local or a global file (depending on
/// the namespace) opens the procedure and goes to the
/// corresponding position.
/////////////////////////////////////////////////
void NumeReEditor::FindAndOpenProcedure(const wxString& procedurename)
{
    if (!procedurename.length())
		return;

	vector<std::string> vPaths = m_terminal->getPathSettings();
	wxString pathname = procedurename;

	// Resolve the namespaces
	if (pathname.find("$this~") != string::npos)
	{
	    // Get the current namespace and replace it
		wxString thispath = GetFileNameAndPath();
		pathname.replace(pathname.find("$this~"), 6, thispath.substr(0, thispath.rfind('\\') + 1));
	}
	else if (pathname.find("$thisfile~") != string::npos)
	{
	    // Search for the procedure in the current file
		wxString procedurename = pathname.substr(pathname.rfind('~') + 1);
		wxString procedureline;
		int nminpos = 0;
		int nmaxpos = GetLastPosition();

		// Search the procedure and check, whether it is uncommented
		while (nminpos < nmaxpos && FindText(nminpos, nmaxpos, "procedure", wxSTC_FIND_MATCHCASE | wxSTC_FIND_WHOLEWORD) != -1)
		{
			nminpos = FindText(nminpos, nmaxpos, "procedure", wxSTC_FIND_MATCHCASE | wxSTC_FIND_WHOLEWORD) + 1;

			// Check for comments
			if (this->GetStyleAt(nminpos) == wxSTC_NSCR_COMMENT_BLOCK || this->GetStyleAt(nminpos) == wxSTC_NSCR_COMMENT_LINE)
				continue;

			procedureline = GetLine(LineFromPosition(nminpos));

			// If the line contains the necessary syntax elements
			// jump to it
			if (procedureline.find("$" + procedurename) != string::npos && procedureline[procedureline.find_first_not_of(' ', procedureline.find("$" + procedurename) + procedurename.length() + 1)] == '(')
			{
				this->SetFocus();
				this->GotoLine(LineFromPosition(nminpos));
				this->SetFirstVisibleLine(VisibleFromDocLine(LineFromPosition(nminpos))-2);
				this->EnsureVisible(LineFromPosition(nminpos));
				return;
			}
		}

		// If it is not found, ask the user for creation
		int ret = wxMessageBox(_guilang.get("GUI_DLG_PROC_NEXISTS_CREATE", procedurename.ToStdString()), _guilang.get("GUI_DLG_PROC_NEXISTS_CREATE_HEADLINE"), wxCENTER | wxICON_WARNING | wxYES_NO, this);

		if (ret != wxYES)
			return;

        // Get the template
		wxString proctemplate = getTemplateContent(procedurename);

		// Add the template after the last line
		int nLastLine = this->GetLineCount();
		this->GotoLine(nLastLine);
		this->AddText("\n");
		this->AddText(proctemplate);

		// Replace the name in the template with the correct name
		// and goto the position
		nLastLine = FindText(this->PositionFromLine(nLastLine), this->GetLastPosition(), "procedure $" + procedurename, wxSTC_FIND_MATCHCASE);
		this->GotoPipe(nLastLine);

		// Update the syntax highlighting and the analyzer state
		UpdateSyntaxHighlighting(true);
		m_analyzer->run();

		return;
	}
	else
	{
	    // Usual case: replace the namespace syntax with
	    // the path syntax
		if (pathname.find("$main~") != string::npos)
			pathname.erase(pathname.find("$main~") + 1, 5);

		while (pathname.find('~') != string::npos)
			pathname[pathname.find('~')] = '/';

		if (pathname[0] == '$' && pathname.find(':') == string::npos)
			pathname.replace(0, 1, vPaths[5] + "/");
		else if (pathname.find(':') == string::npos)
			pathname.insert(0, vPaths[5]);
		else // pathname.find(':') != string::npos
		{
			pathname = pathname.substr(pathname.find('\'') + 1, pathname.rfind('\'') - pathname.find('\'') - 1);
		}
	}

	wxArrayString pathnames;
	pathnames.Add(pathname + ".nprc");

	// If the file with this path exists, open it,
	// otherwise ask the user for creation
	if (!fileExists((pathname + ".nprc").ToStdString()))
	{
		int ret = wxMessageBox(_guilang.get("GUI_DLG_PROC_NEXISTS_CREATE", procedurename.ToStdString()), _guilang.get("GUI_DLG_PROC_NEXISTS_CREATE_HEADLINE"), wxCENTER | wxICON_WARNING | wxYES_NO, this);

		if (ret != wxYES)
			return;

		m_mainFrame->NewFile(FILE_NPRC, pathname);
	}
	else
		m_mainFrame->OpenSourceFile(pathnames);
}


/////////////////////////////////////////////////
/// \brief Finds the included script and displays
/// it in the editor.
///
/// \param includename const wxString&
/// \return void
///
/// This private member function searches for the included
/// script as a global file and opens it, if it exists.
/////////////////////////////////////////////////
void NumeReEditor::FindAndOpenInclude(const wxString& includename)
{
    if (!includename.length())
        return;

    wxArrayString pathnames;
	pathnames.Add(includename);

	// If the file exists, open it, otherwise
	// ask the user for creation
	if (!fileExists((includename).ToStdString()))
	{
		int ret = wxMessageBox(_guilang.get("GUI_DLG_SCRIPT_NEXISTS_CREATE", includename.ToStdString()), _guilang.get("GUI_DLG_SCRIPT_NEXISTS_CREATE_HEADLINE"), wxCENTER | wxICON_WARNING | wxYES_NO, this);

		if (ret != wxYES)
			return;

		m_mainFrame->NewFile(FILE_NSCR, includename);
	}
	else
		m_mainFrame->OpenSourceFile(pathnames);
}


/////////////////////////////////////////////////
/// \brief Wrapper for the search controller.
///
/// \return vector<wxString>
///
/////////////////////////////////////////////////
vector<wxString> NumeReEditor::getProceduresInFile()
{
    return m_search->getProceduresInFile();
}


/////////////////////////////////////////////////
/// \brief Replaces the matches with a new symbol.
///
/// \param vMatches const vector<int>&
/// \param sSymbol const wxString&
/// \param sNewSymbol const wxString&
/// \return void
///
/// This member function replaces the found matches
/// with a new symbol.
/////////////////////////////////////////////////
void NumeReEditor::ReplaceMatches(const vector<int>& vMatches, const wxString& sSymbol, const wxString& sNewSymbol)
{
    // During the replacement, the positions are moving
    // this variable tracks the differences
    int nInc = sNewSymbol.length() - sSymbol.length();

    // Do nothing, if no match was found
    if (!vMatches.size())
        return;

    // replace every match with the new symbol name
    for (size_t i = 0; i < vMatches.size(); i++)
    {
        this->Replace(vMatches[i] + i*nInc, vMatches[i]+sSymbol.length() + i*nInc, sNewSymbol);
    }
}


/////////////////////////////////////////////////
/// \brief Asks the user to supply a new name for
/// the symbol at the passed position and replaces
/// all occurences.
///
/// \param nPos int
/// \return void
///
/// This member function replaces the selected symbol
/// with a new name supplied by the user using a
/// text input dialog. This method also supplies the
/// needed user interaction.
/////////////////////////////////////////////////
void NumeReEditor::RenameSymbols(int nPos)
{
    wxString sNewName;
    wxString sCurrentName;

    int nStartPos = 0;
    int nEndPos = this->GetLastPosition();

    // Find the current symbol's name and ensure that it
    // exists
    sCurrentName = this->GetTextRange(this->WordStartPosition(nPos, true), this->WordEndPosition(nPos, true));

    if (!sCurrentName.length())
        return;

    // Change the position to the start of the selected symbol
    // to correctly identify the style of the selected symbol
    nPos = this->WordStartPosition(nPos, true);

    // Prepare and show the text entry dialog, so that the
    // user may supply a new symbol name
    RenameSymbolsDialog textdialog(this, vRenameSymbolsChangeLog, wxID_ANY, _guilang.get("GUI_DLG_RENAMESYMBOLS"), sCurrentName);
    int retval = textdialog.ShowModal();

    if (retval == wxID_CANCEL)
        return;

    // Get the new symbol name and ensure that it
    // exists
    sNewName = textdialog.GetValue();

    if (!sNewName.length() || (!textdialog.replaceAfterCursor() && !textdialog.replaceBeforeCursor()))
        return;

    vRenameSymbolsChangeLog.push_back(sCurrentName + "\t" + sNewName);

    // The selected symbol is probably part of a procedure. If this is
    // the case, get the start and end position here
    if ((m_fileType == FILE_NPRC || m_fileType == FILE_MATLAB) && !textdialog.replaceInWholeFile())
    {
        // Find the head of the current procedure
        nStartPos = m_search->FindCurrentProcedureHead(nPos);

        // Find the end of the current procedure depending on
        // the located head
        vector<int> vBlock = this->BlockMatch(nStartPos);
        if (vBlock.back() != wxSTC_INVALID_POSITION)
            nEndPos = vBlock.back();
    }

    // Adjust start and end position depending
    // on the flags of the renaming dialog
    if (!textdialog.replaceAfterCursor())
    {
        nEndPos = WordEndPosition(nPos, true);
    }
    else if (!textdialog.replaceBeforeCursor())
    {
        nStartPos = WordStartPosition(nPos, true);
    }

    // Ensure that the new symbol is not already in use
    vector<int> vNewNameOccurences = m_search->FindAll(sNewName, GetStyleAt(nPos), nStartPos, nEndPos);

    // It's possible to rename a standard function with
    // a new name. Check here against custom function
    // names
    if (!vNewNameOccurences.size() && isStyleType(STYLE_FUNCTION, nPos))
        vNewNameOccurences = m_search->FindAll(sNewName, wxSTC_NSCR_CUSTOM_FUNCTION, nStartPos, nEndPos);

    // MATLAB-specific fix, because the MATLAB lexer
    // does know custom defined functions
    if (!vNewNameOccurences.size() && m_fileType == FILE_MATLAB && (isStyleType(STYLE_FUNCTION, nPos) || isStyleType(STYLE_COMMAND, nPos)))
        vNewNameOccurences = m_search->FindAll(sNewName, wxSTC_MATLAB_IDENTIFIER, nStartPos, nEndPos);

    // If the vector size is non-zero, this symbol is
    // already in use
    if (vNewNameOccurences.size())
    {
        // Allow the user to cancel the replacement
        if (wxMessageBox(_guilang.get("GUI_DLG_RENAMESYMBOLS_ALREADYINUSE_WARNING"), _guilang.get("GUI_DLG_RENAMESYMBOLS_ALREADYINUSE"), wxCENTER | wxOK | wxCANCEL | wxICON_EXCLAMATION, this) == wxCANCEL)
            return;
    }

    // Gather all operations into one undo step
    this->BeginUndoAction();

    // Perform the renaming of symbols
    this->ReplaceMatches(m_search->FindAll(sCurrentName, this->GetStyleAt(nPos), nStartPos, nEndPos, textdialog.replaceInComments()), sCurrentName, sNewName);
    this->EndUndoAction();
}


/////////////////////////////////////////////////
/// \brief Extracts the marked selection into a
/// new procedure.
///
/// \return void
///
/// This member function extracts a selected code
/// section into a new procedure. The interface
/// is created depending upon the used variables
/// inside of the selected block.
/////////////////////////////////////////////////
void NumeReEditor::AbstrahizeSection()
{
    // Do nothing, if the user didn't select anything
    if (!HasSelection())
        return;

    // Get start and end position (i.e. the corresponding lines)
    int nStartPos = PositionFromLine(LineFromPosition(GetSelectionStart()));
    int nEndPos = GetLineEndPosition(LineFromPosition(GetSelectionEnd()));
    if (GetSelectionEnd() == PositionFromLine(LineFromPosition(GetSelectionEnd())))
        nEndPos = GetLineEndPosition(LineFromPosition(GetSelectionEnd()-1));

    int nCurrentBlockStart = 0;
    int nCurrentBlockEnd = GetLastPosition();

    list<wxString> lInputTokens;
    list<wxString> lOutputTokens;

    set<string> sArgumentListSet;
    set<string> sMatlabReturnListSet;


    // If we have a procedure file, consider scoping
    if (m_fileType == FILE_NPRC || m_fileType == FILE_MATLAB)
    {
        nCurrentBlockStart = m_search->FindCurrentProcedureHead(nStartPos);

        string sArgumentList = getFunctionArgumentList(LineFromPosition(nCurrentBlockStart)).ToStdString();

        // Split the argument list into single tokens
        while (sArgumentList.length())
        {
            sArgumentListSet.insert(getNextArgument(sArgumentList, true));
        }

        // In the MATLAB case, get the return list of
        // the function
        if (m_fileType == FILE_MATLAB)
        {
            string sReturnList = getMatlabReturnList(LineFromPosition(nCurrentBlockStart)).ToStdString();

            // Split the return list into single tokens
            while (sReturnList.length())
            {
                sMatlabReturnListSet.insert(getNextArgument(sReturnList, true));
            }

        }

        // Ensure that the end of the current block exists
        vector<int> vBlock = BlockMatch(nCurrentBlockStart);

        if (vBlock.back() != wxSTC_INVALID_POSITION && vBlock.back() > nEndPos)
            nCurrentBlockEnd = vBlock.back();

        // Increment the starting line to omit the argument list
        // as variable occurence source. Those will be detected
        // by comparing with the splitted argument list
        nCurrentBlockStart = PositionFromLine(LineFromPosition(nCurrentBlockStart)+1);
    }

    // Determine the interface by searching for variables
    // and tables, which are used before or after the code
    // section and occure inside of the section
    for (int i = nStartPos; i <= nEndPos; i++)
    {
        if (isStyleType(STYLE_IDENTIFIER, i) || isStyleType(STYLE_STRINGPARSER, i))
        {
            // Regular indices
            //
            // Jump over string parser characters
            while (isStyleType(STYLE_STRINGPARSER, i) && (GetCharAt(i) == '#' || GetCharAt(i) == '~'))
                i++;

            if (isStyleType(STYLE_OPERATOR, i))
                continue;

            // Get the token name
            wxString sCurrentToken = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

            // Ignore MATLAB structure fields
            if (GetCharAt(WordStartPosition(i, true)-1) == '.')
                continue;

            // Find all occurences
            vector<int> vMatch = m_search->FindAll(sCurrentToken, this->GetStyleAt(i), nCurrentBlockStart, nCurrentBlockEnd);

            if (vMatch.size())
            {
                // Determine, whether the token is used before
                // or afer the current section
                if (vMatch.front() < nStartPos || (sArgumentListSet.size() && sArgumentListSet.find(sCurrentToken.ToStdString()) != sArgumentListSet.end()))
                    lInputTokens.push_back(sCurrentToken);

                if (vMatch.back() > nEndPos && vMatch.front() < nStartPos && IsModifiedInSection(nStartPos, nEndPos, sCurrentToken, vMatch))
                    lOutputTokens.push_back(sCurrentToken);
                else if (vMatch.back() > nEndPos && vMatch.front() >= nStartPos && sArgumentListSet.find(sCurrentToken.ToStdString()) == sArgumentListSet.end())
                    lOutputTokens.push_back(sCurrentToken);
                else if (sMatlabReturnListSet.size() && sMatlabReturnListSet.find(sCurrentToken.ToStdString()) != sMatlabReturnListSet.end() && IsModifiedInSection(nStartPos, nEndPos, sCurrentToken, vMatch))
                    lOutputTokens.push_back(sCurrentToken);
            }

            i += sCurrentToken.length();
        }
        else if (isStyleType(STYLE_FUNCTION, i) && m_fileType == FILE_MATLAB)
        {
            // Matlab specials
            //
            // Get the token name
            wxString sCurrentToken = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

            // Ignore MATLAB structure fields
            if (GetCharAt(WordStartPosition(i, true)-1) == '.')
                continue;

            // Ignore functions, which are not part of any argument list;
            // these are most probably actual functions
            if ((!sMatlabReturnListSet.size() || sMatlabReturnListSet.find(sCurrentToken.ToStdString()) == sMatlabReturnListSet.end())
                && (!sArgumentListSet.size() || sArgumentListSet.find(sCurrentToken.ToStdString()) == sMatlabReturnListSet.end()))
                continue;

            // Find all occurences
            vector<int> vMatch = m_search->FindAll(sCurrentToken, this->GetStyleAt(i), nCurrentBlockStart, nCurrentBlockEnd);

            if (vMatch.size())
            {
                // Determine, whether the token is used before
                // or afer the current section
                if (sArgumentListSet.size() && sArgumentListSet.find(sCurrentToken.ToStdString()) != sArgumentListSet.end())
                    lInputTokens.push_back(sCurrentToken);

                if (sMatlabReturnListSet.size() && sMatlabReturnListSet.find(sCurrentToken.ToStdString()) != sMatlabReturnListSet.end() && IsModifiedInSection(nStartPos, nEndPos, sCurrentToken, vMatch))
                    lOutputTokens.push_back(sCurrentToken);
            }

            i += sCurrentToken.length();
        }
        else if (isStyleType(STYLE_CUSTOMFUNCTION, i))
        {
            // Tables
            //
            // Get the token name
            wxString sCurrentToken = GetTextRange(WordStartPosition(i, true), WordEndPosition(i, true));

            // Find all occurences
            vector<int> vMatch = m_search->FindAll(sCurrentToken, this->GetStyleAt(i), nCurrentBlockStart, nCurrentBlockEnd);

            if (vMatch.size())
            {
                // Determine, whether the token is used before
                // or afer the current section
                if (vMatch.front() < nStartPos || vMatch.back() > nEndPos)
                    lInputTokens.push_back(sCurrentToken + "()");
            }

            i += sCurrentToken.length();
        }
    }

    // Only use each token once. We use the list
    // functionalities of C++
    if (lInputTokens.size())
    {
        lInputTokens.sort();
        lInputTokens.unique();
    }

    if (lOutputTokens.size())
    {
        lOutputTokens.sort();
        lOutputTokens.unique();
    }

    wxString sInputList;
    wxString sOutputList;

    // Create the interface definition lists
    for (auto iter = lInputTokens.begin(); iter != lInputTokens.end(); ++iter)
    {
        sInputList += *iter + ",";
    }

    if (sInputList.length())
        sInputList.erase(sInputList.length()-1);

    for (auto iter = lOutputTokens.begin(); iter != lOutputTokens.end(); ++iter)
    {
        sOutputList += *iter + ",";
    }

    if (sOutputList.length())
        sOutputList.erase(sOutputList.length()-1);

    // Use these interfaces and the positions to
    // create the new procedure in a new window
    CreateProcedureFromSection(nStartPos, nEndPos, sInputList, sOutputList);
}


/////////////////////////////////////////////////
/// \brief Creates a new procedure from the analysed
/// code section.
///
/// \param nStartPos int
/// \param nEndPos int
/// \param sInputList const wxString&
/// \param sOutputList const wxString
/// \return void
///
/// This member function is used to create a new
/// procedure from the analysed code section (Done
/// in \c AbstrahizeSection()). The new procedure
/// will be displayed in a new window.
/////////////////////////////////////////////////
void NumeReEditor::CreateProcedureFromSection(int nStartPos, int nEndPos, const wxString& sInputList, const wxString sOutputList)
{
    // Creata a new window and a new editor
    ViewerFrame* copyFrame = new ViewerFrame(m_mainFrame, _guilang.get("GUI_REFACTORING_COPYWINDOW_HEAD"));
    NumeReEditor* edit = new NumeReEditor(m_mainFrame, m_options, new ProjectInfo(true), copyFrame, wxID_ANY, _syntax, m_terminal, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);
    wxStatusBar* statusbar = copyFrame->CreateStatusBar();
    int sizes[] = {-2, -1};
    statusbar->SetFieldsCount(2, sizes);
    statusbar->SetStatusText(_guilang.get("GUI_STATUSBAR_UNSAVEDFILE"), 0);

    // Fill the editor with the new procedure and its
    // determined interface
    if (m_fileType == FILE_NSCR || m_fileType == FILE_NPRC)
    {
        // Set a default file name with the corresponding
        // extension
        edit->SetFilename(wxFileName("numere.nprc"), false);
        edit->SetText("\r\n");

        // Write some preface comment
        edit->AddText("## " + _guilang.get("GUI_REFACTORING_NOTE") + "\r\n");
        edit->AddText("##\r\n");
        edit->AddText("## " + _guilang.get("GUI_REFACTORING_ARGUMENTLIST") + "\r\n");

        // Write the input argument list
        edit->AddText("procedure $NEWPROCEDURE(" + sInputList + ")\r\n");

        // Write the actual section of code
        edit->AddText(this->GetTextRange(nStartPos, nEndPos) + "\r\n");

        // Write the output list
        if (sOutputList.length())
        {
            edit->AddText("\t## " + _guilang.get("GUI_REFACTORING_RETURNVALUES") + "\r\n");
            if (sOutputList.find(',') != string::npos)
                edit->AddText("return {" + sOutputList + "};\r\n");
            else
                edit->AddText("return " + sOutputList + ";\r\n");
        }
        else
            edit->AddText("return void\r\n");
        edit->AddText("endprocedure\r\n");
        statusbar->SetStatusText(_guilang.get("GUI_STATUSBAR_NPRC"), 1);
    }
    else if (m_fileType == FILE_MATLAB)
    {
        // Set a default file name with the corresponding
        // extension
        edit->SetFilename(wxFileName("numere.m"), false);
        edit->SetText("\r\n");

        // Write some preface comment
        edit->AddText("% " + _guilang.get("GUI_REFACTORING_NOTE") + "\r\n");
        edit->AddText("%\r\n");
        edit->AddText("% " + _guilang.get("GUI_REFACTORING_ARGUMENTLIST") + "\r\n");
        if (sOutputList.length())
            edit->AddText("% " + _guilang.get("GUI_REFACTORING_RETURNVALUES") + "\r\n");

        edit->AddText("function ");

        // Write the output list
        if (sOutputList.length())
        {
            edit->AddText("[" + sOutputList + "] = ");
        }

        // Write the input argument list
        edit->AddText("NEWFUNCTION(" + sInputList + ")\r\n");

        // Write the actual section of code
        edit->AddText(this->GetTextRange(nStartPos, nEndPos) + "\r\n");
        edit->AddText("end\r\n");
        statusbar->SetStatusText(_guilang.get("GUI_STATUSBAR_M"), 1);
    }

    // Update the syntax highlighting
    // and use the autoformatting feature
    edit->UpdateSyntaxHighlighting(true);
    edit->ApplyAutoFormat(0, -1);
    edit->ToggleSettings(SETTING_WRAPEOL);

    // Set a reasonable window size and
    // display it to the user
    copyFrame->SetSize(800, 600);
    copyFrame->SetIcon(wxIcon(m_mainFrame->getProgramFolder() + "\\icons\\icon.ico", wxBITMAP_TYPE_ICO));
    copyFrame->Show();
    copyFrame->SetFocus();
}


/////////////////////////////////////////////////
/// \brief Determines, whether \c sToken is modified
/// in the section.
///
/// \param nSectionStart int
/// \param nSectionEnd int
/// \param sToken const wxString&
/// \param vMatch const vector<int>&
/// \return bool
///
/// This member function determines, whether a
/// string token, which corresponds to a variable,
/// is semantically modified in the code section
/// (i.e. overwritten). This is used for variables,
/// which are both input and possible output.
/////////////////////////////////////////////////
bool NumeReEditor::IsModifiedInSection(int nSectionStart, int nSectionEnd, const wxString& sToken, const vector<int>& vMatch)
{
    // define the set of modifying operators
    static wxString sModificationOperators = "+= -= /= ^= *= ++ --";

    // Go through all occurences of the current token
    for (size_t i = 0; i < vMatch.size(); i++)
    {
        // Ignore occurences before or after the current
        // code section
        if (vMatch[i] < nSectionStart)
            continue;
        if (vMatch[i] > nSectionEnd)
            break;

        // Ignore dynamic structure field accesses in MATLAB
        if (isStyleType(STYLE_OPERATOR, vMatch[i]-1) && (GetCharAt(vMatch[i]-1) == '.' || (GetCharAt(vMatch[i]-2) == '.' && GetCharAt(vMatch[i]-1) == '(')))
            continue;

        // Examine the code part left of the token, whether
        // there's a modifying operator
        for (int j = vMatch[i]+sToken.length(); j < nSectionEnd; j++)
        {
            // Ignore whitespaces
            if (GetCharAt(j) == ' ' || GetCharAt(j) == '\t')
                continue;

            // We only examine operator characters
            if (isStyleType(STYLE_OPERATOR, j) && isStyleType(STYLE_OPERATOR, j+1) && sModificationOperators.find(GetTextRange(j, j+2)) != string::npos)
                return true;
            else if (isStyleType(STYLE_OPERATOR, j) && GetCharAt(j) == '=' && GetCharAt(j+1) != '=')
                return true;
            else if (isStyleType(STYLE_OPERATOR, j) && (GetCharAt(j) == '(' || GetCharAt(j) == '[' || GetCharAt(j) == '{'))
            {
                // Jump over parentheses
                j = BraceMatch(j);
                if (j == wxSTC_INVALID_POSITION)
                    return false;
            }
            else if (isStyleType(STYLE_OPERATOR, j) && (GetCharAt(j) == ')' || GetCharAt(j) == ']' || GetCharAt(j) == '}' || GetCharAt(j) == ':'))
            {
                // ignore closing parentheses
                continue;
            }
            else if (isStyleType(STYLE_OPERATOR, j) && GetCharAt(j) == '.' && isStyleType(STYLE_IDENTIFIER, j+1))
            {
                // MATLAB struct fix
                while (isStyleType(STYLE_IDENTIFIER, j+1))
                    j++;
            }
            else if (isStyleType(STYLE_OPERATOR, j) && GetCharAt(j) == '.' && isStyleType(STYLE_FUNCTION, j+1))
            {
                // MATLAB struct fix
                while (isStyleType(STYLE_FUNCTION, j+1))
                    j++;
            }
            else if (isStyleType(STYLE_OPERATOR, j) && GetCharAt(j) == '.' && GetCharAt(j+1) == '(')
            {
                // MATLAB struct fix
                // Jump over parentheses
                j = BraceMatch(j+1);
                if (j == wxSTC_INVALID_POSITION)
                    return false;
            }
            else if (isStyleType(STYLE_OPERATOR, j) && GetCharAt(j) == ',')
            {
                // Try to find the end of the current bracket
                // or the current brace - we don't use parentheses
                // as terminators, because they are not used to
                // gather return values
                for (int k = j; k < nSectionEnd; k++)
                {
                    if (isStyleType(STYLE_OPERATOR, k) && (GetCharAt(k) == ']' || GetCharAt(k) == '}'))
                    {
                        j = k;
                        break;
                    }
                    else if (isStyleType(STYLE_OPERATOR, k) && GetCharAt(k) == ';')
                        break;
                }
            }
            else
                break;
        }
    }

    return false;
}


/////////////////////////////////////////////////
/// \brief Gets the argument list of a procedure.
///
/// \param nFunctionStartLine int
/// \return wxString
///
/// This private member function extracts the argument
/// list from procedures, which is necessary
/// for correct function extraction.
/////////////////////////////////////////////////
wxString NumeReEditor::getFunctionArgumentList(int nFunctionStartLine)
{
    // Get the complete line
    wxString sReturn = GetLine(nFunctionStartLine);

    // Ensure that the line contains the keyword "function"
    // or "procedure", respectively
    if ((sReturn.find("function ") == string::npos && m_fileType == FILE_MATLAB)
        || (sReturn.find("procedure ") == string::npos && m_fileType == FILE_NPRC))
        return "";

    // Ensure that the line contains opening and
    // closing parentheses
    if (sReturn.find('(') == string::npos || sReturn.find(')') == string::npos)
        return "";

    // Extract the function argument list
    sReturn.erase(0, sReturn.find('(')+1);
    sReturn.erase(sReturn.rfind(')'));

    // Return the argument list
    return sReturn;
}


/////////////////////////////////////////////////
/// \brief Gets the return list of a MATLAB function.
///
/// \param nMatlabFunctionStartLine int
/// \return wxString
///
/// This private member function extracts the return
/// list from MATLAB functions, which is necessary
/// for correct function extraction.
/////////////////////////////////////////////////
wxString NumeReEditor::getMatlabReturnList(int nMatlabFunctionStartLine)
{
    // Get the complete line
    wxString sReturn = GetLine(nMatlabFunctionStartLine);

    // Ensure that the line contains the keyword "function"
    if (sReturn.find("function") == string::npos)
        return "";

    // Ensure that the line contains an equal sign
    if (sReturn.find('=') == string::npos)
        return "";

    // Remove the keyword part and the function declaration itself
    sReturn.erase(0, sReturn.find("function")+8);
    sReturn.erase(sReturn.find('='));

    // Remove surrounding brackets
    if (sReturn.find('[') != string::npos && sReturn.find(']') != string::npos)
    {
        sReturn.erase(0, sReturn.find('[')+1);
        sReturn.erase(sReturn.rfind(']'));
    }

    // Return the return list
    return sReturn;
}


/////////////////////////////////////////////////
/// \brief Returns the contents of a template file.
///
/// \param sFileName const wxString&
/// \return wxString
///
/// The contents of a template file are written to
/// memory and the place holders are filled with
/// target file name and timestamp.
/////////////////////////////////////////////////
wxString NumeReEditor::getTemplateContent(const wxString& sFileName)
{
	wxString template_file, template_type, timestamp;

	template_type = "tmpl_procedure.nlng";
	timestamp = getTimeStamp(false);

	// Get the file's contents
	if (m_terminal->getKernelSettings().getUseCustomLanguageFiles() && wxFileExists(m_mainFrame->getProgramFolder() + "\\user\\lang\\" + template_type))
		m_mainFrame->GetFileContents(m_mainFrame->getProgramFolder() + "\\user\\lang\\" + template_type, template_file, template_type);
	else
		m_mainFrame->GetFileContents(m_mainFrame->getProgramFolder() + "\\lang\\" + template_type, template_file, template_type);

    // Replace filenames
	while (template_file.find("%%1%%") != string::npos)
		template_file.replace(template_file.find("%%1%%"), 5, sFileName);

    // Replace timestamps
	while (template_file.find("%%2%%") != string::npos)
		template_file.replace(template_file.find("%%2%%"), 5, timestamp);

	return template_file;
}


/////////////////////////////////////////////////
/// \brief Generates an autocompletion list based
/// upon the file's contents.
///
/// \param wordstart const wxString&
/// \param sPreDefList string
/// \return wxString
///
/// The function combines the passed predefined
/// list of autocompletioon possibities (aka the
/// actual syntax autocompletion list) with possible
/// completion candidates found in the text of the
/// current file.
/////////////////////////////////////////////////
wxString NumeReEditor::generateAutoCompList(const wxString& wordstart, string sPreDefList)
{
	map<wxString, int> mAutoCompMap;
	wxString wReturn = "";
	string sCurrentWord = "";

	// Store the list of predefined values in the map
	if (sPreDefList.length())
	{
		while (sPreDefList.length())
		{
			sCurrentWord = sPreDefList.substr(0, sPreDefList.find(' '));

			if (sCurrentWord.find('(') != string::npos)
				mAutoCompMap[toLowerCase(sCurrentWord.substr(0, sCurrentWord.find('('))) + " |" + sCurrentWord] = -1;
			else
				mAutoCompMap[toLowerCase(sCurrentWord.substr(0, sCurrentWord.find('?'))) + " |" + sCurrentWord] = -1;

			sPreDefList.erase(0, sPreDefList.find(' '));

			if (sPreDefList.front() == ' ')
				sPreDefList.erase(0, 1);
		}
	}

	unsigned int nPos = 0;

	// Find every occurence of the current word start
	// and store the possible completions in the map
	while ((nPos = this->FindText(nPos, this->GetLastPosition(), wordstart, wxSTC_FIND_WORDSTART)) != string::npos)
	{
		if (nPos > (size_t)this->GetCurrentPos() || WordEndPosition(nPos + 1, true) < this->GetCurrentPos())
			mAutoCompMap[toLowerCase(this->GetTextRange(nPos, WordEndPosition(nPos + 1, true)).ToStdString()) + " |" + this->GetTextRange(nPos, WordEndPosition(nPos + 1, true))] = 1;

		nPos++;
	}

	// remove duplicates
	for (auto iter = mAutoCompMap.begin(); iter != mAutoCompMap.end(); ++iter)
	{
		if (iter->second == -1)
		{
			if ((iter->first).find('(') != string::npos)
			{
				if (mAutoCompMap.find((iter->first).substr(0, (iter->first).find('('))) != mAutoCompMap.end())
				{
					mAutoCompMap.erase((iter->first).substr(0, (iter->first).find('(')));
					iter = mAutoCompMap.begin();
				}
			}
			else
			{
				if (mAutoCompMap.find((iter->first).substr(0, (iter->first).find('?'))) != mAutoCompMap.end())
				{
					mAutoCompMap.erase((iter->first).substr(0, (iter->first).find('?')));
					iter = mAutoCompMap.begin();
				}
			}
		}
	}

	// Re-combine the autocompletion list
	for (auto iter = mAutoCompMap.begin(); iter != mAutoCompMap.end(); ++iter)
		wReturn += (iter->first).substr((iter->first).find('|') + 1) + " ";

    return wReturn;
}


/////////////////////////////////////////////////
/// \brief Highlights all word occurences permanently.
///
/// \param event wxCommandEvent&
/// \return void
///
/// This member function highlights the clicked word
/// permanently or removes the highlighting, if the
/// word was already selected
/////////////////////////////////////////////////
void NumeReEditor::OnDisplayVariable(wxCommandEvent& event)
{
    // Clear the highlighting state and prepare the
    // colors and style for the next highlighting
	long int maxpos = this->GetLastPosition();
	SetIndicatorCurrent(HIGHLIGHT);
	IndicatorClearRange(0, maxpos);
	IndicatorSetStyle(HIGHLIGHT, wxSTC_INDIC_ROUNDBOX);
	IndicatorSetAlpha(HIGHLIGHT, 100);
	IndicatorSetForeground(HIGHLIGHT, wxColor(255, 0, 0));

	unsigned int nPos = 0;
	unsigned int nCurr = 0;
	vector<unsigned int> vSelectionList;

	// If the current clicked word is already
	// highlighted, then simply clear out the
	// buffer string and return
	if (m_watchedString == m_clickedWord)
    {
        m_watchedString.clear();
        return;
    }

    // Update the buffer with the new clicked word
	m_watchedString = m_clickedWord;

	// Search for all occurences of the current clicked
	// word in the document and store them
	while ((nPos = this->FindText(nCurr, maxpos, m_clickedWord, wxSTC_FIND_MATCHCASE | wxSTC_FIND_WHOLEWORD)) != string::npos)
	{
		vSelectionList.push_back(nPos);
		nCurr = nPos +  m_clickedWordLength;
	}

    // Apply the indicator to all found occurences
	for (size_t i = 0; i < vSelectionList.size(); i++)
	{
		this->IndicatorFillRange(vSelectionList[i], m_clickedWordLength);
	}
}


/////////////////////////////////////////////////
/// \brief Triggers the main frame to show the
/// documentation viewer concerning the selected
/// command.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnHelpOnSelection(wxCommandEvent& event)
{
	m_mainFrame->ShowHelp(m_clickedWord.ToStdString());
}


/////////////////////////////////////////////////
/// \brief Private event handler function for
/// finding the procedure definition.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnFindProcedure(wxCommandEvent& event)
{
	if (!m_clickedProcedure.length())
		return;

	FindAndOpenProcedure(m_clickedProcedure);
}


/////////////////////////////////////////////////
/// \brief Global event handler function for
/// finding the procedure definition.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnFindProcedureFromMenu()
{
    if (!isNumeReFileType() || GetStyleAt(GetCurrentPos()) != wxSTC_NSCR_PROCEDURES)
        return;

    m_search->FindMarkedProcedure(GetCurrentPos());
    FindAndOpenProcedure(m_clickedProcedure);
}


/////////////////////////////////////////////////
/// \brief Private event handler function for
/// finding the included script.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnFindInclude(wxCommandEvent& event)
{
	if (!m_clickedInclude.length())
		return;

	FindAndOpenInclude(m_clickedInclude);
}


/////////////////////////////////////////////////
/// \brief Global event handler function for
/// finding the included script.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnFindIncludeFromMenu()
{
    if (!isNumeReFileType() || GetStyleAt(GetCurrentPos()) != wxSTC_NSCR_INCLUDES)
        return;

    m_search->FindMarkedInclude(GetCurrentPos());
    FindAndOpenInclude(m_clickedInclude);
}


/////////////////////////////////////////////////
/// \brief Changes the letters in the selection.
///
/// \param event wxCommandEvent& Using \c GetId
/// method to determine upper or lowercase
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnChangeCase(wxCommandEvent& event)
{
	if (!HasSelection())
		return;

    // Get selection positions
	int nFirstPos = GetSelectionStart();
	int nLastPos = GetSelectionEnd();

	// Change the case
	if (event.GetId() == ID_UPPERCASE)
		Replace(nFirstPos, nLastPos, toUpperCase(GetSelectedText().ToStdString()));
	else
		Replace(nFirstPos, nLastPos, toLowerCase(GetSelectedText().ToStdString()));
}


/////////////////////////////////////////////////
/// \brief Event wrapper for \c FoldCurrentBlock.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnFoldCurrentBlock(wxCommandEvent& event)
{
	FoldCurrentBlock(this->LineFromPosition(this->PositionFromPoint(m_lastRightClick)));
}


/////////////////////////////////////////////////
/// \brief Private event handling function for
/// hiding the selection.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnHideSelection(wxCommandEvent& event)
{
    int nFirstLine = LineFromPosition(GetSelectionStart());
    int nLastLine = LineFromPosition(GetSelectionEnd());

    HideLines(nFirstLine, nLastLine);

    MarkerAdd(nFirstLine-1, MARKER_HIDDEN);
    MarkerAdd(nFirstLine-1, MARKER_HIDDEN_MARGIN);
}


/////////////////////////////////////////////////
/// \brief Global event handling function to unhide
/// all lines from the main frame's menu.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnUnhideAllFromMenu()
{
    if (GetAllLinesVisible())
        return;

    ShowLines(0, LineFromPosition(GetLastPosition()));

    MarkerDeleteAll(MARKER_HIDDEN);
    MarkerDeleteAll(MARKER_HIDDEN_MARGIN);
}


/////////////////////////////////////////////////
/// \brief Event wrapper for \c RenameSymbols.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnRenameSymbols(wxCommandEvent& event)
{
    this->RenameSymbols(this->PositionFromPoint(m_lastRightClick));
}


/////////////////////////////////////////////////
/// \brief Global wrapper for \c RenameSymbols.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnRenameSymbolsFromMenu()
{
    int charpos = GetCurrentPos();

    if (this->isStyleType(STYLE_DEFAULT, charpos) || this->isStyleType(STYLE_IDENTIFIER, charpos) || this->isStyleType(STYLE_DATAOBJECT, charpos) || this->isStyleType(STYLE_FUNCTION, charpos))
        this->RenameSymbols(charpos);
}


/////////////////////////////////////////////////
/// \brief Private event handler for extracting
/// the selected section.
///
/// \param event wxCommandEvent&
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnAbstrahizeSection(wxCommandEvent& event)
{
    this->AbstrahizeSection();
}


/////////////////////////////////////////////////
/// \brief Global event handler for extracting
/// the selected section.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnAbstrahizeSectionFromMenu()
{
    if (HasSelection())
        this->AbstrahizeSection();
}


/////////////////////////////////////////////////
/// \brief Displays the duplicated code dialog.
///
/// \return bool
///
/////////////////////////////////////////////////
bool NumeReEditor::InitDuplicateCode()
{
	if (m_fileType == FILE_NSCR || m_fileType == FILE_NPRC || m_fileType == FILE_MATLAB || m_fileType == FILE_CPP)
	{
		m_duplicateCode = new DuplicateCodeDialog(this, "NumeRe: " + _guilang.get("GUI_DUPCODE_TITLE") + " [" + this->GetFilenameString() + "]");
		m_duplicateCode->SetIcon(wxIcon(m_mainFrame->getProgramFolder() + "\\icons\\icon.ico", wxBITMAP_TYPE_ICO));
		m_duplicateCode->Show();
		m_duplicateCode->SetFocus();
		m_duplicateCode->Refresh();
		return true;
	}

	return false;
}


/////////////////////////////////////////////////
/// \brief Wrapper for \c detectCodeDuplicates.
///
/// \param nDuplicateFlag int
/// \param nNumDuplicatedLines int
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::OnFindDuplicateCode(int nDuplicateFlag, int nNumDuplicatedLines)
{
	detectCodeDuplicates(0, this->LineFromPosition(this->GetLastPosition()), nDuplicateFlag, nNumDuplicatedLines);
}


/////////////////////////////////////////////////
/// \brief Thread event handler function for the
/// duplicated code detection functionality.
///
/// \param event wxThreadEvent&
/// \return void
///
/// This function simply updates the gauge in the
/// duplicated code dialog and passes the results,
/// once the detection is finished.
/////////////////////////////////////////////////
void NumeReEditor::OnThreadUpdate(wxThreadEvent& event)
{
	if (m_nProcessValue < 100)
	{
	    // Update the gauge
		if (m_duplicateCode && m_duplicateCode->IsShown())
			m_duplicateCode->SetProgress(m_nProcessValue);
	}
	else
	{
	    // Update the gauge
		if (m_duplicateCode && m_duplicateCode->IsShown())
			m_duplicateCode->SetProgress(100);
		else
		{
			vDuplicateCodeResults.clear();
			return;
		}

		// Pass the results
		wxCriticalSectionLocker lock(m_editorCS);
		m_duplicateCode->SetResult(vDuplicateCodeResults);
		vDuplicateCodeResults.clear();
	}

}


/////////////////////////////////////////////////
/// \brief Highlights differences between two blocks
/// of code.
///
/// \param nStart1 int
/// \param nEnd1 int
/// \param nStart2 int
/// \param nEnd2 int
/// \param nSelectionLine int
/// \return void
///
/// This function highlights the differences between
/// two blocks of code including the wordwise
/// differences in the lines.
///
/// \todo Move the marker and indicator definitions.
/////////////////////////////////////////////////
void NumeReEditor::IndicateDuplicatedLine(int nStart1, int nEnd1, int nStart2, int nEnd2, int nSelectionLine)
{
	MarkerDefine(MARKER_DUPLICATEINDICATOR_ONE, wxSTC_MARK_BACKGROUND);
	MarkerSetBackground(MARKER_DUPLICATEINDICATOR_ONE, wxColour(220, 255, 220));
	MarkerDefine(MARKER_DUPLICATEINDICATOR_TWO, wxSTC_MARK_BACKGROUND);
	MarkerSetBackground(MARKER_DUPLICATEINDICATOR_TWO, wxColour(255, 220, 220));
	MarkerDeleteAll(MARKER_DUPLICATEINDICATOR_ONE);
	MarkerDeleteAll(MARKER_DUPLICATEINDICATOR_TWO);

	SetIndicatorCurrent(HIGHLIGHT_DIFFERENCES);
	IndicatorSetStyle(HIGHLIGHT_DIFFERENCES, wxSTC_INDIC_ROUNDBOX);
	IndicatorSetAlpha(HIGHLIGHT_DIFFERENCES, 64);
	IndicatorSetForeground(HIGHLIGHT_DIFFERENCES, wxColour(128, 0, 128));

	IndicatorClearRange(0, GetLastPosition());

	SetIndicatorCurrent(HIGHLIGHT_DIFFERENCE_SOURCE);
	IndicatorSetStyle(HIGHLIGHT_DIFFERENCE_SOURCE, wxSTC_INDIC_ROUNDBOX);
	IndicatorSetAlpha(HIGHLIGHT_DIFFERENCE_SOURCE, 64);
	IndicatorSetForeground(HIGHLIGHT_DIFFERENCE_SOURCE, wxColour(0, 128, 128));

	IndicatorClearRange(0, GetLastPosition());

	if (nStart1 == -1 && nStart2 == -1 && nEnd1 == -1 && nEnd2 == -1)
		return;

    // Mark section 1
	for (int i = nStart1; i <= nEnd1; i++)
		MarkerAdd(i, MARKER_DUPLICATEINDICATOR_ONE);

    // Mark section 2
	for (int i = nStart2; i <= nEnd2; i++)
		MarkerAdd(i, MARKER_DUPLICATEINDICATOR_TWO);

    // Determine the wordwise differences
	map<int, int> mDifferences = getDifferences(nStart1, nEnd1, nStart2, nEnd2);

	// Mark the wordwise differences
	for (auto iter = mDifferences.begin(); iter != mDifferences.end(); ++iter)
	{
		if ((iter->first) < 0)
			SetIndicatorCurrent(HIGHLIGHT_DIFFERENCE_SOURCE);
		else
			SetIndicatorCurrent(HIGHLIGHT_DIFFERENCES);

		IndicatorFillRange(abs(iter->first), iter->second);
	}

	ScrollToLine(nSelectionLine);
}


/////////////////////////////////////////////////
/// \brief Removes the double-click occurence
/// indicators from the document.
///
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::ClearDblClkIndicator()
{
	if (!m_dblclkString.length())
		return;

	m_dblclkString.clear();

	SetIndicatorCurrent(HIGHLIGHT_DBLCLK);
	long int maxpos = GetLastPosition();
	IndicatorClearRange(0, maxpos);
}


/////////////////////////////////////////////////
/// \brief Event handler called when clicking on the
/// editor margin.
///
/// \param event wxStyledTextEvent&
/// \return void
///
/// This event handler function toggles the folding,
/// if one clicks on the fold margin, and handles
/// the breakpoints or the hidden lines markers.
/////////////////////////////////////////////////
void NumeReEditor::OnMarginClick( wxStyledTextEvent& event )
{
	bool bCanUseBreakPoints = m_fileType == FILE_NSCR || m_fileType == FILE_NPRC;
	int position = event.GetPosition();
	int linenum = LineFromPosition(position);

	if (event.GetMargin() == MARGIN_FOLD)
	{
	    // Folding logic
		int levelClick = GetFoldLevel(linenum);

		if ((levelClick & wxSTC_FOLDLEVELHEADERFLAG) > 0)
			ToggleFold(linenum);
	}
	else
	{
	    // All other markers
	    if (MarkerOnLine(linenum, MARKER_HIDDEN))
        {
            // Hidden lines
            int nNextVisibleLine = linenum+1;

            while (!GetLineVisible(nNextVisibleLine))
                nNextVisibleLine++;

            ShowLines(linenum, nNextVisibleLine);

            for (int i = linenum; i < nNextVisibleLine; i++)
            {
                MarkerDelete(i, MARKER_HIDDEN);
                MarkerDelete(i, MARKER_HIDDEN_MARGIN);
            }

        }
		else if (MarkerOnLine(linenum, MARKER_BREAKPOINT) && bCanUseBreakPoints)
		{
		    // Breakpoint
			RemoveBreakpoint(linenum);
		}
		else if (bCanUseBreakPoints)
		{
		    // Add breakpoint
			AddBreakpoint(linenum);
		}
	}
}


/////////////////////////////////////////////////
/// \brief Adds a breakpoint to the selected line.
///
/// \param linenum int
/// \return void
///
/// This function checks in advance, whether the
/// selected line is non-empty and not a comment-
/// only line.
/////////////////////////////////////////////////
void NumeReEditor::AddBreakpoint( int linenum )
{
    // Go through the line and ensure that this
    // line actually contains executable code
	for (int i = PositionFromLine(linenum); i < GetLineEndPosition(linenum); i++)
	{
	    // Check the current character
		if (GetStyleAt(i) != wxSTC_NSCR_COMMENT_BLOCK
				&& GetStyleAt(i) != wxSTC_NSCR_COMMENT_LINE
				&& GetCharAt(i) != '\r'
				&& GetCharAt(i) != '\n'
				&& GetCharAt(i) != ' '
				&& GetCharAt(i) != '\t')
		{
		    // Add the breakpoint marker
			int markerNum = MarkerAdd(linenum, MARKER_BREAKPOINT);

			// Add the breakpoint to the internal
			// logic
			m_breakpoints.Add(markerNum);
			CreateBreakpointEvent(linenum, true);
			m_terminal->addBreakpoint(GetFileNameAndPath().ToStdString(), linenum);
			break;
		}
	}
}


/////////////////////////////////////////////////
/// \brief Removes a breakpoint from the selected
/// line.
///
/// \param linenum int
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::RemoveBreakpoint( int linenum )
{
	// need to remove the marker handle from the array - use
	// LineFromHandle on debug start and clean up then
	MarkerDelete(linenum, MARKER_BREAKPOINT);
	CreateBreakpointEvent(linenum, false);
	m_terminal->removeBreakpoint(GetFileNameAndPath().ToStdString(), linenum);
}


/////////////////////////////////////////////////
/// \brief Synchronizes all breakpoints between
/// editor and kernel.
///
/// \return void
///
/// This member function synchronizes all breakpoints
/// in the current opened file after the file was
/// modified and saved
/////////////////////////////////////////////////
void NumeReEditor::SynchronizeBreakpoints()
{
    // Clear all breakpoints stored internally
    m_terminal->clearBreakpoints(GetFileNameAndPath().ToStdString());
    int line = 0;

    // Re-set the existing breakpoints
    while ((line = MarkerNext(line, 1 << MARKER_BREAKPOINT)) != -1)
    {
        m_terminal->addBreakpoint(GetFileNameAndPath().ToStdString(), line);
        line++;
    }
}


/////////////////////////////////////////////////
/// \brief Checks, whether the passed marker is set
/// on the passed line.
///
/// \param linenum int
/// \param nMarker int
/// \return bool
///
/////////////////////////////////////////////////
bool NumeReEditor::MarkerOnLine(int linenum, int nMarker)
{
	int markerLineBitmask = this->MarkerGet(linenum);
	return (markerLineBitmask & (1 << nMarker));
}


/////////////////////////////////////////////////
/// \brief Main thread loop for the duplicated
/// code analysis.
///
/// \return wxThread::ExitCode
///
/////////////////////////////////////////////////
wxThread::ExitCode NumeReEditor::Entry()
{
	vDuplicateCodeResults.clear();

	// Create the memory for the parsed line
	// This is done before the processing to avoid reallocs during the process
	vParsedSemanticCode.resize(m_nLastLine + 1);

	double dMatch = 0.0;
	int nLongestMatch = 0;
	int nBlankLines = 0;
	int nLastStatusVal = 0;
	int currentDuplicateCodeLength = m_nDuplicateCodeLines;

	if (getFileType() == FILE_CPP)
		currentDuplicateCodeLength *= 2;

    // Go through the selected code
	for (int i = m_nFirstLine; i <= m_nLastLine - currentDuplicateCodeLength; i++)
	{
		if (GetThread()->TestDestroy())
			break;

		if (m_duplicateCode && m_duplicateCode->IsShown())
		{
			// display some status value
			if ((int)((double)i / (double)(m_nLastLine - currentDuplicateCodeLength - m_nFirstLine) * 100) != nLastStatusVal)
			{
				nLastStatusVal = (double)i / (double)(m_nLastLine - currentDuplicateCodeLength - m_nFirstLine) * 100;
				wxCriticalSectionLocker lock(m_editorCS);
				m_nProcessValue = nLastStatusVal;
				wxQueueEvent(GetEventHandler(), new wxThreadEvent());
			}
		}
		else
		{
			// Stop this processing, if the duplicate code window was closed
			break;
		}

		// Search for all possible duplications in the remaining document.
		// We don't have to search before the current line, because we would
		// have found this duplication earlier.
		for (int j = i + currentDuplicateCodeLength; j <= m_nLastLine - currentDuplicateCodeLength; j++)
		{
			dMatch = compareCodeLines(i, j, m_nDuplicateCodeFlag);

			// The match should be at least 75%
			if (dMatch >= 0.75)
			{
				double dComp;

				// Search the following code lines for a continuation
				// of the current match
				for (int k = 1; k <= m_nLastLine - j; k++)
				{
					dComp = compareCodeLines(i + k, j + k, m_nDuplicateCodeFlag);

					if (dComp == -1.0)
					{
						nBlankLines++;
						continue;
					}
					else if (dComp < 0.75 || i + k == j)
					{
					    // Was the last line of the duplication. Is it long
					    // enough?
						if (k - nBlankLines > currentDuplicateCodeLength)
						{
						    // Add the current duplication to the buffer
							wxCriticalSectionLocker lock(m_editorCS);
							vDuplicateCodeResults.push_back(toString(i + 1) + "-" + toString(i + k) + " == " + toString(j + 1) + "-" + toString(j + k) + " [" + toString(dMatch * 100.0 / (double)(k - nBlankLines), 3) + " %]");
							if (nLongestMatch < k - 1 - nBlankLines)
								nLongestMatch = k - 1 - nBlankLines;
						}

						break;
					}
					else
						dMatch += dComp;
				}

				nBlankLines = 0;
			}
			else if (dMatch < 0.0) // empty line at pos i
				break;
		}

		i += nLongestMatch;
		nLongestMatch = 0;
	}

	// clear the parsed code
	vParsedSemanticCode.clear();

	wxCriticalSectionLocker lock(m_editorCS);
	m_nProcessValue = 100;
	wxQueueEvent(GetEventHandler(), new wxThreadEvent());
	return (wxThread::ExitCode)0;
}


/////////////////////////////////////////////////
/// \brief Starts the duplicated code analysis.
///
/// \param startline int
/// \param endline int
/// \param nDuplicateFlags int
/// \param nNumDuplicatedLines int
/// \return void
///
/// This function starts the secondary thread,
/// which will handle the actual analysis, because
/// it is very time and resource consuming and would
/// block the editor otherwise.
/////////////////////////////////////////////////
void NumeReEditor::detectCodeDuplicates(int startline, int endline, int nDuplicateFlags, int nNumDuplicatedLines)
{
	m_nDuplicateCodeFlag = nDuplicateFlags;
	m_nFirstLine = startline;
	m_nLastLine = endline;
	m_nDuplicateCodeLines = nNumDuplicatedLines;

	if (CreateThread(wxTHREAD_DETACHED) != wxTHREAD_NO_ERROR)
		return;

	if (GetThread()->Run() != wxTHREAD_NO_ERROR)
		return;
}


/////////////////////////////////////////////////
/// \brief Performs a semantic code comparsion of
/// the two selected lines.
///
/// \param nLine1 int
/// \param nLine2 int
/// \param nDuplicateFlag int
/// \return double
///
/// This function performs a semantic code comparison
/// between two code lines. Will return a double
/// representing a matching percentage. This value
/// is constructed out of a semantic match.
/// If the line lengths differ too much, the
/// analysis is omitted.
/////////////////////////////////////////////////
double NumeReEditor::compareCodeLines(int nLine1, int nLine2, int nDuplicateFlag)
{
	size_t nMatchedCount = 0;

	// Get the code lines transformed into semantic
	// code
	string sSemLine1 = this->getSemanticLine(nLine1, nDuplicateFlag);
	string sSemLine2 = this->getSemanticLine(nLine2, nDuplicateFlag);

	// It is possible that the lines are semantical identical although they may contain different vars
	if (!sSemLine1.length() && sSemLine2.length())
		return -2.0;
	else if (!sSemLine1.length() && !sSemLine2.length())
		return -1.0;
	else if (sSemLine1.length() * 1.5 < sSemLine2.length() || sSemLine1.length() > sSemLine2.length() * 1.5)
		return 0.0;

    // Check the actual match of the semantic code
	for (size_t i = 0; i < sSemLine1.length(); i++)
	{
		if (i >= sSemLine2.length())
			break;

		if (sSemLine1[i] == sSemLine2[i])
			nMatchedCount++;
	}

	return (double)nMatchedCount / (double)max(sSemLine1.length(), sSemLine2.length());
}


/////////////////////////////////////////////////
/// \brief Returns the selected line as semantic
/// code.
///
/// \param nLine int
/// \param nDuplicateFlag int
/// \return string
///
/// If the selected line was already transformed
/// into semantic code, the semantic code is read
/// from a buffer.
/////////////////////////////////////////////////
string NumeReEditor::getSemanticLine(int nLine, int nDuplicateFlag)
{
	if (vParsedSemanticCode[nLine].length())
		return vParsedSemanticCode[nLine];

    // Use the correct parser for the current language
	if (getFileType() == FILE_NSCR || getFileType() == FILE_NPRC)
		return getSemanticLineNSCR(nLine, nDuplicateFlag);
	else if (getFileType() == FILE_MATLAB)
		return getSemanticLineMATLAB(nLine, nDuplicateFlag);
	else if (getFileType() == FILE_CPP)
		return getSemanticLineCPP(nLine, nDuplicateFlag);
	else
		return "";
}


/////////////////////////////////////////////////
/// \brief Returns the selected line as semantic
/// code.
///
/// \param nLine int
/// \param nDuplicateFlag int
/// \return string
///
/// This function parses NumeRe code into semantic
/// code and returns it. The degree of transformation
/// is selected using a bit or in \c nDuplicateFlag:
/// \li 0 = direct comparison
/// \li 1 = use var semanticals
/// \li 2 = use string semanticals
/// \li 4 = use numeric semanticals
/////////////////////////////////////////////////
string NumeReEditor::getSemanticLineNSCR(int nLine, int nDuplicateFlag)
{
	string sSemLine = "";

	for (int i = this->PositionFromLine(nLine); i < this->GetLineEndPosition(nLine); i++)
	{
		if (this->GetCharAt(i) == ' '
				|| this->GetCharAt(i) == '\t'
				|| this->GetCharAt(i) == '\r'
				|| this->GetCharAt(i) == '\n'
				|| this->GetStyleAt(i) == wxSTC_NSCR_COMMENT_BLOCK
				|| this->GetStyleAt(i) == wxSTC_NSCR_COMMENT_LINE)
			continue;
		else if ((nDuplicateFlag & SEMANTICS_VAR)
				 && (this->GetStyleAt(i) == wxSTC_NSCR_DEFAULT
					 || this->GetStyleAt(i) == wxSTC_NSCR_DEFAULT_VARS
					 || this->GetStyleAt(i) == wxSTC_NSCR_IDENTIFIER))
		{
			// replace vars with a placeholder
			i = this->WordEndPosition(i, true) - 1;

			while (this->GetStyleAt(i + 1) == wxSTC_NSCR_DEFAULT
					|| this->GetStyleAt(i + 1) == wxSTC_NSCR_DEFAULT_VARS
					|| this->GetStyleAt(i + 1) == wxSTC_NSCR_IDENTIFIER)
				i++;

			sSemLine += "VAR";
		}
		else if ((nDuplicateFlag & SEMANTICS_STRING) && this->GetStyleAt(i) == wxSTC_NSCR_STRING)
		{
			// replace string literals with a placeholder
			i++;

			while (this->GetStyleAt(i + 1) == wxSTC_NSCR_STRING)
				i++;

			sSemLine += "STR";
		}
		else if ((nDuplicateFlag & SEMANTICS_NUM) && this->GetStyleAt(i) == wxSTC_NSCR_NUMBERS)
		{
			// replace numeric literals with a placeholder
			while (this->GetStyleAt(i + 1) == wxSTC_NSCR_NUMBERS)
				i++;

			if (sSemLine.back() == '-' || sSemLine.back() == '+')
			{
				if (sSemLine.length() == 1)
					sSemLine.clear();
				else
				{
					char cDelim = sSemLine[sSemLine.length() - 2];

					if (cDelim == ':' || cDelim == '=' || cDelim == '?' || cDelim == ',' || cDelim == ';' || cDelim == '(' || cDelim == '[' || cDelim == '{')
						sSemLine.pop_back();
				}
			}

			sSemLine += "NUM";
		}
		else if ((nDuplicateFlag & SEMANTICS_FUNCTION) && this->GetStyleAt(i) == wxSTC_NSCR_CUSTOM_FUNCTION)
		{
			// replace functions and caches with a placeholder
			while (this->GetStyleAt(i + 1) == wxSTC_NSCR_CUSTOM_FUNCTION)
				i++;

			sSemLine += "FUNC";
		}
		else
			sSemLine += this->GetCharAt(i);

	}

	// Store the result to avoid repeated processing of this line
	vParsedSemanticCode[nLine] = sSemLine;
	return sSemLine;
}


/////////////////////////////////////////////////
/// \brief Returns the selected line as semantic
/// code.
///
/// \param nLine int
/// \param nDuplicateFlag int
/// \return string
///
/// This function parses MATLAB code into semantic
/// code and returns it. The degree of transformation
/// is selected using a bit or in \c nDuplicateFlag:
/// \li 0 = direct comparison
/// \li 1 = use var semanticals
/// \li 2 = use string semanticals
/// \li 4 = use numeric semanticals
/////////////////////////////////////////////////
string NumeReEditor::getSemanticLineMATLAB(int nLine, int nDuplicateFlag)
{
	string sSemLine = "";

	for (int i = this->PositionFromLine(nLine); i < this->GetLineEndPosition(nLine); i++)
	{
		if (this->GetCharAt(i) == ' '
				|| this->GetCharAt(i) == '\t'
				|| this->GetCharAt(i) == '\r'
				|| this->GetCharAt(i) == '\n'
				|| this->GetStyleAt(i) == wxSTC_MATLAB_COMMENT)
			continue;
		else if ((nDuplicateFlag & SEMANTICS_VAR)
				 && (this->GetStyleAt(i) == wxSTC_MATLAB_DEFAULT
					 || this->GetStyleAt(i) == wxSTC_MATLAB_IDENTIFIER))
		{
			// replace vars with a placeholder
			i = this->WordEndPosition(i, true) - 1;

			while (this->GetStyleAt(i + 1) == wxSTC_MATLAB_DEFAULT
					|| this->GetStyleAt(i + 1) == wxSTC_MATLAB_IDENTIFIER)
				i++;

			sSemLine += "VAR";
		}
		else if ((nDuplicateFlag & SEMANTICS_STRING) && this->GetStyleAt(i) == wxSTC_MATLAB_STRING)
		{
			// replace string literals with a placeholder
			i++;

			while (this->GetStyleAt(i + 1) == wxSTC_MATLAB_STRING)
				i++;

			sSemLine += "STR";
		}
		else if ((nDuplicateFlag & SEMANTICS_NUM) && this->GetStyleAt(i) == wxSTC_MATLAB_NUMBER)
		{
			// replace numeric literals with a placeholder
			while (this->GetStyleAt(i + 1) == wxSTC_MATLAB_NUMBER)
				i++;

			if (sSemLine.back() == '-' || sSemLine.back() == '+')
			{
				if (sSemLine.length() == 1)
					sSemLine.clear();
				else
				{
					char cDelim = sSemLine[sSemLine.length() - 2];

					if (cDelim == ':' || cDelim == '=' || cDelim == '?' || cDelim == ',' || cDelim == ';' || cDelim == '(' || cDelim == '[' || cDelim == '{')
						sSemLine.pop_back();
				}
			}

			sSemLine += "NUM";
		}
		else
			sSemLine += this->GetCharAt(i);

	}

	// Store the result to avoid repeated processing of this line
	vParsedSemanticCode[nLine] = sSemLine;
	return sSemLine;
}


/////////////////////////////////////////////////
/// \brief Returns the selected line as semantic
/// code.
///
/// \param nLine int
/// \param nDuplicateFlag int
/// \return string
///
/// This function parses C++ code into semantic
/// code and returns it. The degree of transformation
/// is selected using a bit or in \c nDuplicateFlag:
/// \li 0 = direct comparison
/// \li 1 = use var semanticals
/// \li 2 = use string semanticals
/// \li 4 = use numeric semanticals
/////////////////////////////////////////////////
string NumeReEditor::getSemanticLineCPP(int nLine, int nDuplicateFlag)
{
	string sSemLine = "";

	for (int i = this->PositionFromLine(nLine); i < this->GetLineEndPosition(nLine); i++)
	{
		if (this->GetCharAt(i) == ' '
				|| this->GetCharAt(i) == '\t'
				|| this->GetCharAt(i) == '\r'
				|| this->GetCharAt(i) == '\n'
				|| isStyleType(STYLE_COMMENT_LINE, i) || isStyleType(STYLE_COMMENT_BLOCK, i))
			continue;
		else if ((nDuplicateFlag & SEMANTICS_VAR)
				 && (this->GetStyleAt(i) == wxSTC_C_DEFAULT
					 || this->GetStyleAt(i) == wxSTC_C_IDENTIFIER))
		{
			// replace vars with a placeholder
			i = this->WordEndPosition(i, true) - 1;

			while (this->GetStyleAt(i + 1) == wxSTC_C_DEFAULT
					|| this->GetStyleAt(i + 1) == wxSTC_C_IDENTIFIER)
				i++;

			sSemLine += "VAR";
		}
		else if ((nDuplicateFlag & SEMANTICS_STRING) && (this->GetStyleAt(i) == wxSTC_C_STRING || this->GetStyleAt(i) == wxSTC_C_CHARACTER))
		{
			// replace string literals with a placeholder
			i++;

			while (this->GetStyleAt(i + 1) == wxSTC_C_STRING || this->GetStyleAt(i + 1) == wxSTC_C_CHARACTER)
				i++;

			sSemLine += "STR";
		}
		else if ((nDuplicateFlag & SEMANTICS_NUM) && this->GetStyleAt(i) == wxSTC_C_NUMBER)
		{
			// replace numeric literals with a placeholder
			while (this->GetStyleAt(i + 1) == wxSTC_C_NUMBER)
				i++;

			if (sSemLine.back() == '-' || sSemLine.back() == '+')
			{
				if (sSemLine.length() == 1)
					sSemLine.clear();
				else
				{
					char cDelim = sSemLine[sSemLine.length() - 2];

					if (cDelim == ':' || cDelim == '=' || cDelim == '?' || cDelim == ',' || cDelim == ';' || cDelim == '(' || cDelim == '[' || cDelim == '{')
						sSemLine.pop_back();
				}
			}

			sSemLine += "NUM";
		}
		else
			sSemLine += this->GetCharAt(i);

	}

	// Store the result to avoid repeated processing of this line
	vParsedSemanticCode[nLine] = sSemLine;
	return sSemLine;
}


/////////////////////////////////////////////////
/// \brief Returns the actual word-wise differences
/// in the selected lines.
///
/// \param nStart1 int
/// \param nEnd1 int
/// \param nStart2 int
/// \param nEnd2 int
/// \return map<int, int>
///
/// This function examines the selected blocks of
/// code linewise and compares every syntax element
/// using \c getNextToken. If they differ, their
/// positions and lengths are stored in the returned
/// map.
/////////////////////////////////////////////////
map<int, int> NumeReEditor::getDifferences(int nStart1, int nEnd1, int nStart2, int nEnd2)
{
	map<int, int> mDifferences;
	int nLinePos1 = 0;
	int nLinePos2 = 0;
	wxString sToken1;
	wxString sToken2;

	// Compare every line in the selected range
	for (int i = 0; i <= nEnd1 - nStart1; i++)
	{
		nLinePos1 = this->PositionFromLine(nStart1 + i);
		nLinePos2 = this->PositionFromLine(nStart2 + i);

		// Read every token from the lines and compare them
		while (nLinePos1 < this->GetLineEndPosition(nStart1 + i) || nLinePos2 < this->GetLineEndPosition(nStart2 + i))
		{
			sToken1 = getNextToken(nLinePos1);
			sToken2 = getNextToken(nLinePos2);

			// Break, if no tokens are available
			if (!sToken1.length() && !sToken2.length())
				break;

            // Compare the tokens
			if (sToken1.length() && !sToken2.length())
				mDifferences[-nLinePos1] = sToken1.length();
			else if (sToken2.length() && !sToken1.length())
				mDifferences[nLinePos2] = sToken2.length();
			else
			{
				if (sToken1 != sToken2)
				{
					mDifferences[-nLinePos1] = sToken1.length();
					mDifferences[nLinePos2] = sToken2.length();
				}
			}

			// Increment the search position
			nLinePos1 += sToken1.length();
			nLinePos2 += sToken2.length();

			if (nLinePos1 > this->GetLineEndPosition(nStart1 + i))
				nLinePos1 = this->GetLineEndPosition(nStart1 + i);

			if (nLinePos2 > this->GetLineEndPosition(nStart2 + i))
				nLinePos2 = this->GetLineEndPosition(nStart2 + i);
		}
	}

	return mDifferences;
}


/////////////////////////////////////////////////
/// \brief Returns the next syntax token starting
/// from the selected position.
///
/// \param nPos int&
/// \return wxString
///
/////////////////////////////////////////////////
wxString NumeReEditor::getNextToken(int& nPos)
{
	int nCurrentLineEnd = this->GetLineEndPosition(this->LineFromPosition(nPos));

	// return nothing, if already at line end
	if (nPos >= nCurrentLineEnd)
		return "";

	int nCurrentStyle;
	int nEndPos;

	// forward over all whitespace characters
	while (this->GetCharAt(nPos) == ' '
			|| this->GetCharAt(nPos) == '\t'
			|| this->GetCharAt(nPos) == '\r'
			|| this->GetCharAt(nPos) == '\n')
		nPos++;

	// return nothing, if already at line end (set position to line end)
	if (nPos >= nCurrentLineEnd)
	{
		nPos = nCurrentLineEnd;
		return "";
	}

	// get the current style
	nCurrentStyle = this->GetStyleAt(nPos);
	nEndPos = nPos;

	// while the style is identical forward the end position
	while (this->GetStyleAt(nEndPos) == nCurrentStyle)
		nEndPos++;

	// it is possible that we walked over the last position
	if (nEndPos > nCurrentLineEnd)
		return this->GetTextRange(nPos, nCurrentLineEnd);

	return this->GetTextRange(nPos, nEndPos);
}


/////////////////////////////////////////////////
/// \brief Wrapper for \c CodeFormatter.
///
/// \param nFirstLine int
/// \param nLastLine int
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::ApplyAutoIndentation(int nFirstLine, int nLastLine)
{
	m_formatter->IndentCode(nFirstLine, nLastLine);
}


/////////////////////////////////////////////////
/// \brief Determine the syntax style type at the
/// selected position.
///
/// \param _type StyleType
/// \param nPos int
/// \return bool
///
/// This member function summarizes determining,
/// which style type the selected character is
/// using abstracting out the selection of the
/// correct styling language.
/////////////////////////////////////////////////
bool NumeReEditor::isStyleType(StyleType _type, int nPos)
{
	switch (this->getFileType())
	{
		case FILE_NSCR:
		case FILE_NPRC:
			{
				switch (_type)
				{
					case STYLE_DEFAULT:
						return this->GetStyleAt(nPos) == wxSTC_NSCR_DEFAULT;
					case STYLE_COMMENT_LINE:
						return this->GetStyleAt(nPos) == wxSTC_NSCR_COMMENT_LINE;
					case STYLE_COMMENT_BLOCK:
						return this->GetStyleAt(nPos) == wxSTC_NSCR_COMMENT_BLOCK;
					case STYLE_COMMENT_SECTION_LINE:
						return this->GetStyleAt(nPos) == wxSTC_NSCR_COMMENT_LINE && this->GetTextRange(nPos, nPos + 3) == "##!";
					case STYLE_COMMENT_SECTION_BLOCK:
						return this->GetStyleAt(nPos) == wxSTC_NSCR_COMMENT_BLOCK && this->GetTextRange(nPos, nPos + 3) == "#*!";
					case STYLE_COMMAND:
						return this->GetStyleAt(nPos) == wxSTC_NSCR_COMMAND
							   || this->GetStyleAt(nPos) == wxSTC_NPRC_COMMAND;
					case STYLE_FUNCTION:
						return this->GetStyleAt(nPos) == wxSTC_NSCR_FUNCTION;
                    case STYLE_CUSTOMFUNCTION:
                        return this->GetStyleAt(nPos) == wxSTC_NSCR_CUSTOM_FUNCTION;
                    case STYLE_OPERATOR:
                        return this->GetStyleAt(nPos) == wxSTC_NSCR_OPERATORS;
                    case STYLE_PROCEDURE:
                        return this->GetStyleAt(nPos) == wxSTC_NSCR_PROCEDURES;
                    case STYLE_IDENTIFIER:
                        return this->GetStyleAt(nPos) == wxSTC_NSCR_IDENTIFIER;
                    case STYLE_DATAOBJECT:
                        return this->GetStyleAt(nPos) == wxSTC_NSCR_CUSTOM_FUNCTION || this->GetStyleAt(nPos) == wxSTC_NSCR_CLUSTER;
                    case STYLE_NUMBER:
                        return this->GetStyleAt(nPos) == wxSTC_NSCR_NUMBERS;
                    case STYLE_STRINGPARSER:
                        return this->GetStyleAt(nPos) == wxSTC_NSCR_STRING_PARSER;
                    case STYLE_STRING:
                        return this->GetStyleAt(nPos) == wxSTC_NSCR_STRING;
				}
				break;
			}
		case FILE_MATLAB:
			{
				switch (_type)
				{
					case STYLE_DEFAULT:
						return this->GetStyleAt(nPos) == wxSTC_MATLAB_DEFAULT;
					case STYLE_COMMENT_LINE:
						return this->GetStyleAt(nPos) == wxSTC_MATLAB_COMMENT;
					case STYLE_COMMENT_BLOCK:
						return this->GetStyleAt(nPos) == wxSTC_MATLAB_COMMENT;
					case STYLE_COMMENT_SECTION_LINE:
						return this->GetStyleAt(nPos) == wxSTC_MATLAB_COMMENT && this->GetTextRange(nPos, nPos + 2) == "%%";
					case STYLE_COMMENT_SECTION_BLOCK:
						return this->GetStyleAt(nPos) == wxSTC_MATLAB_COMMENT && this->GetTextRange(nPos, nPos + 2) == "%%";
					case STYLE_COMMAND:
						return this->GetStyleAt(nPos) == wxSTC_MATLAB_KEYWORD;
					case STYLE_FUNCTION:
						return this->GetStyleAt(nPos) == wxSTC_MATLAB_FUNCTIONS;
                    case STYLE_CUSTOMFUNCTION:
                        return false;
                    case STYLE_OPERATOR:
                        return this->GetStyleAt(nPos) == wxSTC_MATLAB_OPERATOR;
                    case STYLE_PROCEDURE:
                        return false;
                    case STYLE_IDENTIFIER:
                        return this->GetStyleAt(nPos) == wxSTC_MATLAB_IDENTIFIER;
                    case STYLE_DATAOBJECT:
                        return false;
                    case STYLE_NUMBER:
                        return this->GetStyleAt(nPos) == wxSTC_MATLAB_NUMBER;
                    case STYLE_STRINGPARSER:
                        return false;
                    case STYLE_STRING:
                        return this->GetStyleAt(nPos) == wxSTC_MATLAB_STRING;
				}
				break;
			}
		case FILE_CPP:
			{
				switch (_type)
				{
					case STYLE_DEFAULT:
						return this->GetStyleAt(nPos) == wxSTC_C_DEFAULT;
					case STYLE_COMMENT_LINE:
						return this->GetStyleAt(nPos) == wxSTC_C_COMMENTLINE
							   || this->GetStyleAt(nPos) == wxSTC_C_COMMENTLINEDOC
							   || this->GetStyleAt(nPos) == wxSTC_C_COMMENTDOCKEYWORD;
					case STYLE_COMMENT_BLOCK:
						return this->GetStyleAt(nPos) == wxSTC_C_COMMENT
							   || this->GetStyleAt(nPos) == wxSTC_C_COMMENTDOC
							   || this->GetStyleAt(nPos) == wxSTC_C_COMMENTDOCKEYWORD;
					case STYLE_COMMENT_SECTION_LINE:
					case STYLE_COMMENT_SECTION_BLOCK:
						return false;
					case STYLE_COMMAND:
						return this->GetStyleAt(nPos) == wxSTC_C_WORD;
					case STYLE_FUNCTION:
						return this->GetStyleAt(nPos) == wxSTC_C_WORD2;
                    case STYLE_CUSTOMFUNCTION:
                        return false;
                    case STYLE_OPERATOR:
                        return this->GetStyleAt(nPos) == wxSTC_C_OPERATOR;
                    case STYLE_PROCEDURE:
                        return false;
                    case STYLE_IDENTIFIER:
                        return this->GetStyleAt(nPos) == wxSTC_C_IDENTIFIER;
                    case STYLE_DATAOBJECT:
                        return false;
                    case STYLE_NUMBER:
                        return this->GetStyleAt(nPos) == wxSTC_C_NUMBER;
                    case STYLE_STRINGPARSER:
                        return false;
                    case STYLE_STRING:
                        return this->GetStyleAt(nPos) == wxSTC_C_STRING;
				}
				break;
			}
		default:
			return false;
	}
	return false;
}


/////////////////////////////////////////////////
/// \brief Counts the german umlauts in the current
/// string.
///
/// \param sStr const string&
/// \return int
///
/////////////////////////////////////////////////
int NumeReEditor::countUmlauts(const string& sStr)
{
	int nUmlauts = 0;
	for (size_t i = 0; i < sStr.length(); i++)
	{
		if (sStr[i] == '�'
				|| sStr[i] == '�'
				|| sStr[i] == '�'
				|| sStr[i] == '�'
				|| sStr[i] == '�'
				|| sStr[i] == '�'
				|| sStr[i] == '�'
				|| sStr[i] == '�'
				|| sStr[i] == '�'
				|| sStr[i] == (char)142
				|| sStr[i] == (char)132
				|| sStr[i] == (char)153
				|| sStr[i] == (char)148
				|| sStr[i] == (char)154
				|| sStr[i] == (char)129
				|| sStr[i] == (char)225
				|| sStr[i] == (char)167
				|| sStr[i] == (char)230
		   )
			nUmlauts++;
	}
	return nUmlauts;
}


/////////////////////////////////////////////////
/// \brief Re-alignes the passed language string
/// to fit into a call tip.
///
/// \param sLine string
/// \param lastpos size_t&
/// \return string
///
/////////////////////////////////////////////////
string NumeReEditor::realignLangString(string sLine, size_t& lastpos)
{
	lastpos = sLine.find(' ');

	if (lastpos == string::npos)
		return sLine;

    // Find the first non-whitespace character
	size_t firstpos = sLine.find_first_not_of(' ', lastpos);

	// Insert separation characters between syntax element
	// and return values
	if (sLine.find(')') < lastpos || sLine.find('.') < lastpos)
		sLine.replace(lastpos, firstpos - lastpos, " -> ");
	else
	{
		if (sLine.find("- ") == firstpos)
			return sLine;

		if (firstpos - lastpos > 2)
		{
			sLine.erase(lastpos, firstpos - lastpos - 2);
			sLine.insert(sLine.find("- "), firstpos - lastpos - 2, ' ');
		}
	}

	return sLine;
}


/////////////////////////////////////////////////
/// \brief Adds linebreaks to the call tip language
/// strings.
///
/// \param sLine const string&
/// \param onlyDocumentation bool
/// \return string
///
/// This member function adds linebreaks at the
/// maximal line length of 100 characters. It is
/// used for the tooltips of functions, commands
/// and procedures
/////////////////////////////////////////////////
string NumeReEditor::addLinebreaks(const string& sLine, bool onlyDocumentation /* = false*/)
{
	const unsigned int nMAXLINE = 100;

	string sReturn = sLine;

	// Remove escaped dollar signs
	while (sReturn.find("\\$") != string::npos)
		sReturn.erase(sReturn.find("\\$"), 1);

	unsigned int nDescStart = sReturn.find("- ");
	unsigned int nIndentPos = 4;
	unsigned int nLastLineBreak = 0;
    bool isItemize = false;

    // Handle the first indent depending on whether this is
    // only a documentation string or a whole definition
	if (onlyDocumentation)
    {
        nDescStart = 0;
        sReturn.insert(0, "    ");
    }
    else
        sReturn.replace(nDescStart, 2, "\n    ");

	if (nDescStart == string::npos)
		return sLine;

	nLastLineBreak = nDescStart;

	for (unsigned int i = nDescStart; i < sReturn.length(); i++)
	{
		if (sReturn[i] == '\n')
        {
			nLastLineBreak = i;

            if (sReturn.substr(i, 7) == "\n    - ")
                isItemize = true;
            else
                isItemize = false;
        }

		if ((i == nMAXLINE && !nLastLineBreak)
				|| (nLastLineBreak && i - nLastLineBreak == nMAXLINE))
		{
			for (int j = i; j >= 0; j--)
			{
				if (sReturn[j] == ' ')
				{
					sReturn[j] = '\n';
					sReturn.insert(j + 1, nIndentPos + 2*isItemize, ' ');
					nLastLineBreak = j;
					break;
				}
				else if (sReturn[j] == '-' && j != (int)i)
				{
					// --> Minuszeichen: nicht immer ist das Trennen an dieser Stelle sinnvoll. Wir pruefen die einfachsten Faelle <--
					if (j &&
							(sReturn[j - 1] == ' '
							 || sReturn[j - 1] == '('
							 || sReturn[j + 1] == ')'
							 || sReturn[j - 1] == '['
							 || (sReturn[j + 1] >= '0' && sReturn[j + 1] <= '9')
							 || sReturn[j + 1] == ','
							 || (sReturn[j + 1] == '"' && sReturn[j - 1] == '"')
							))
						continue;

					sReturn.insert(j + 1, "\n");
					sReturn.insert(j + 2, nIndentPos + 2*isItemize, ' ');
					nLastLineBreak = j + 1;
					break;
				}
				else if (sReturn[j] == ',' && j != (int)i && sReturn[j + 1] != ' ')
				{
					sReturn.insert(j + 1, "\n");
					sReturn.insert(j + 2, nIndentPos + 2*isItemize, ' ');
					nLastLineBreak = j + 1;
					break;
				}
			}
		}
	}

	return sReturn;
}


/////////////////////////////////////////////////
/// \brief Wrapper for \c CodeFormatter.
///
/// \param nFirstLine int
/// \param nLastLine int
/// \return void
///
/////////////////////////////////////////////////
void NumeReEditor::ApplyAutoFormat(int nFirstLine, int nLastLine)
{
    m_formatter->FormatCode(nFirstLine, nLastLine);
}


