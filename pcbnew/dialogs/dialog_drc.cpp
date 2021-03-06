/**
 * @file dialog_drc.cpp
 */

/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2009-2016 Dick Hollenbeck, dick@softplc.com
 * Copyright (C) 2004-2018 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <fctsys.h>
#include <kiface_i.h>
#include <confirm.h>
#include <wildcards_and_files_ext.h>
#include <bitmaps.h>
#include <pgm_base.h>
#include <dialog_drc.h>
#include <pcb_edit_frame.h>
#include <base_units.h>
#include <board_design_settings.h>
#include <class_draw_panel_gal.h>
#include <view/view.h>
#include <collectors.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>

#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>

/* class DIALOG_DRC_CONTROL: a dialog to set DRC parameters (clearance, min cooper size)
 * and run DRC tests
 */

// Keywords for read and write config
#define RefillZonesBeforeDrc        wxT( "RefillZonesBeforeDrc" )


struct BOARD_THAWER
{
    BOARD_THAWER( PCB_EDIT_FRAME* aBoardEditor )
    {
        m_boardEditor = aBoardEditor;
        m_freezeCount = 0;

        while( m_boardEditor->IsFrozen() )
        {
            m_boardEditor->Thaw();
            m_freezeCount++;
        }
    }

    ~BOARD_THAWER()
    {
        while( m_freezeCount > 0 )
        {
            m_boardEditor->Freeze();
            m_freezeCount--;
        }
    }

protected:
    PCB_EDIT_FRAME* m_boardEditor;
    int             m_freezeCount;
};


DIALOG_DRC_CONTROL::DIALOG_DRC_CONTROL( DRC* aTester, PCB_EDIT_FRAME* aEditorFrame,
                                        wxWindow* aParent ) :
    DIALOG_DRC_CONTROL_BASE( aParent ),
    m_trackMinWidth( aEditorFrame, m_TrackMinWidthTitle, m_SetTrackMinWidthCtrl, m_TrackMinWidthUnit, true ),
    m_viaMinSize( aEditorFrame, m_ViaMinTitle, m_SetViaMinSizeCtrl, m_ViaMinUnit, true ),
    m_uviaMinSize( aEditorFrame, m_MicroViaMinTitle, m_SetMicroViakMinSizeCtrl, m_MicroViaMinUnit, true )
{
    m_config = Kiface().KifaceSettings();
    m_tester = aTester;
    m_brdEditor = aEditorFrame;
    m_currentBoard = m_brdEditor->GetBoard();
    m_BrdSettings = m_brdEditor->GetBoard()->GetDesignSettings();

    wxFont messagesLabelFont = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
    messagesLabelFont.SetSymbolicSize( wxFONTSIZE_SMALL );
    m_messagesLabel->SetFont( messagesLabelFont );

    m_BrowseButton->SetBitmap( KiBitmap( folder_xpm ) );

    // We use a sdbSizer here to get the order right, which is platform-dependent
    m_sdbSizer1OK->SetLabel( _( "Run DRC" ) );
    m_sdbSizer1Apply->SetLabel( _( "List Unconnected" ) );
    m_sdbSizer1Cancel->SetLabel( _( "Close" ) );
    m_sizerButtons->Layout();

    m_sdbSizer1OK->SetDefault();

    InitValues();

    // Connect events
    m_ClearanceListBox->Connect( ID_CLEARANCE_LIST, wxEVT_LEFT_DCLICK,
                                 wxMouseEventHandler( DIALOG_DRC_CONTROL::OnLeftDClickClearance ), NULL, this );
    m_ClearanceListBox->Connect( ID_CLEARANCE_LIST, wxEVT_RIGHT_UP,
                                 wxMouseEventHandler( DIALOG_DRC_CONTROL::OnRightUpClearance ), NULL, this );
    m_UnconnectedListBox->Connect( ID_UNCONNECTED_LIST, wxEVT_LEFT_DCLICK,
                                   wxMouseEventHandler( DIALOG_DRC_CONTROL::OnLeftDClickUnconnected ), NULL, this );
    m_UnconnectedListBox->Connect( ID_UNCONNECTED_LIST, wxEVT_RIGHT_UP,
                                   wxMouseEventHandler( DIALOG_DRC_CONTROL::OnRightUpUnconnected ), NULL, this );

    // Now all widgets have the size fixed, call FinishDialogSettings
    FinishDialogSettings();
}


DIALOG_DRC_CONTROL::~DIALOG_DRC_CONTROL()
{
    m_config->Write( RefillZonesBeforeDrc, m_cbRefillZones->GetValue() );

    // Disconnect events
    m_ClearanceListBox->Disconnect( ID_CLEARANCE_LIST, wxEVT_LEFT_DCLICK,
                                    wxMouseEventHandler( DIALOG_DRC_CONTROL::OnLeftDClickClearance ), NULL, this );
    m_ClearanceListBox->Disconnect( ID_CLEARANCE_LIST, wxEVT_RIGHT_UP,
                                    wxMouseEventHandler( DIALOG_DRC_CONTROL::OnRightUpClearance ), NULL, this );
    m_UnconnectedListBox->Disconnect( ID_UNCONNECTED_LIST, wxEVT_LEFT_DCLICK,
                                    wxMouseEventHandler( DIALOG_DRC_CONTROL::OnLeftDClickUnconnected ), NULL, this );
    m_UnconnectedListBox->Disconnect( ID_UNCONNECTED_LIST, wxEVT_RIGHT_UP,
                                    wxMouseEventHandler( DIALOG_DRC_CONTROL::OnRightUpUnconnected ), NULL, this );
}


void DIALOG_DRC_CONTROL::OnActivateDlg( wxActivateEvent& event )
{
    if( m_currentBoard != m_brdEditor->GetBoard() )
    {
        // If m_currentBoard is not the current parent board,
        // (for instance because a new board was loaded),
        // close the dialog, because many pointers are now invalid
        // in lists
        SetReturnCode( wxID_CANCEL );
        Close();
        m_tester->DestroyDRCDialog( wxID_CANCEL );
        return;
    }

    // updating data which can be modified outside the dialog (DRC parameters, units ...)
    // because the dialog is not modal
    m_BrdSettings = m_brdEditor->GetBoard()->GetDesignSettings();
    DisplayDRCValues();
}


void DIALOG_DRC_CONTROL::DisplayDRCValues()
{
    m_trackMinWidth.SetValue( m_BrdSettings.m_TrackMinWidth );
    m_viaMinSize.SetValue( m_BrdSettings.m_ViasMinSize );
    m_uviaMinSize.SetValue( m_BrdSettings.m_MicroViasMinSize );
}


void DIALOG_DRC_CONTROL::InitValues()
{
    m_markersTitleTemplate = m_Notebook->GetPageText( 0 );
    m_unconnectedTitleTemplate = m_Notebook->GetPageText( 1 );

    m_DeleteCurrentMarkerButton->Enable( false );

    DisplayDRCValues();

    // read options
    bool value;
    m_config->Read( RefillZonesBeforeDrc, &value, false );
    m_cbRefillZones->SetValue( value );

    Layout();      // adding the units above expanded Clearance text, now resize.

    SetFocus();
}

/* accept DRC parameters (min clearance value and min sizes
*/
void DIALOG_DRC_CONTROL::SetDrcParmeters( )
{
    m_BrdSettings.m_TrackMinWidth = m_trackMinWidth.GetValue();
    m_BrdSettings.m_ViasMinSize = m_viaMinSize.GetValue();
    m_BrdSettings.m_MicroViasMinSize = m_uviaMinSize.GetValue();

    m_brdEditor->GetBoard()->SetDesignSettings( m_BrdSettings );
}


void DIALOG_DRC_CONTROL::SetRptSettings( bool aEnable, const wxString& aFileName )
{
    m_RptFilenameCtrl->SetValue( aFileName );
    m_CreateRptCtrl->SetValue( aEnable );
}


void DIALOG_DRC_CONTROL::GetRptSettings( bool* aEnable, wxString& aFileName )
{
    *aEnable = m_CreateRptCtrl->GetValue();
    aFileName = m_RptFilenameCtrl->GetValue();
}


void DIALOG_DRC_CONTROL::OnStartdrcClick( wxCommandEvent& event )
{
    wxString reportName, msg;

    bool make_report = m_CreateRptCtrl->IsChecked();

    if( make_report )      // Create a rpt file
    {
        reportName = m_RptFilenameCtrl->GetValue();

        if( reportName.IsEmpty() )
        {
            wxCommandEvent dummy;
            OnButtonBrowseRptFileClick( dummy );
        }

        if( !reportName.IsEmpty() )
            reportName = makeValidFileNameReport();
    }

    SetDrcParmeters();
    m_tester->SetSettings( true,        // Pad to pad DRC test enabled
                           true,        // unconnected pads DRC test enabled
                           true,        // DRC test for zones enabled
                           true,        // DRC test for keepout areas enabled
                           m_cbRefillZones->GetValue(),
                           m_cbReportAllTrackErrors->GetValue(),
                           reportName, make_report );

    DelDRCMarkers();

    wxBeginBusyCursor();
    wxWindowDisabler disabler;

    // run all the tests, with no UI at this time.
    m_Messages->Clear();
    wxSafeYield();                             // Allows time slice to refresh the Messages
    m_brdEditor->GetBoard()->m_Status_Pcb = 0; // Force full connectivity and ratsnest calculations
    m_tester->RunTests(m_Messages);
    m_Notebook->ChangeSelection( 0 );          // display the "Problems/Markers" tab

    // Generate the report
    if( !reportName.IsEmpty() )
    {
        if( writeReport( reportName ) )
        {
            msg.Printf( _( "Report file \"%s\" created" ), GetChars( reportName ) );
            wxMessageDialog popupWindow( this, msg, _( "Disk File Report Completed" ) );
            popupWindow.ShowModal();
        }
        else
        {
            msg.Printf( _( "Unable to create report file \"%s\"" ), GetChars( reportName ) );
            DisplayError( this, msg );
        }
    }

    wxEndBusyCursor();

    RedrawDrawPanel();
}


void DIALOG_DRC_CONTROL::OnDeleteAllClick( wxCommandEvent& event )
{
    DelDRCMarkers();
    RedrawDrawPanel();
    UpdateDisplayedCounts();
}


void DIALOG_DRC_CONTROL::OnListUnconnectedClick( wxCommandEvent& event )
{
    wxString reportName, msg;

    bool make_report = m_CreateRptCtrl->IsChecked();

    if( make_report )      // Create a file rpt
    {
        reportName = m_RptFilenameCtrl->GetValue();

        if( reportName.IsEmpty() )
        {
            wxCommandEvent junk;
            OnButtonBrowseRptFileClick( junk );
        }

        if( !reportName.IsEmpty() )
            reportName = makeValidFileNameReport();
    }

    SetDrcParmeters();

    m_tester->SetSettings( true,        // Pad to pad DRC test enabled
                           true,        // unconnected pads DRC test enabled
                           true,        // DRC test for zones enabled
                           true,        // DRC test for keepout areas enabled
                           m_cbRefillZones->GetValue(),
                           m_cbReportAllTrackErrors->GetValue(),
                           reportName, make_report );

    DelDRCMarkers();

    wxBeginBusyCursor();

    m_Messages->Clear();
    m_tester->ListUnconnectedPads();

    m_Notebook->ChangeSelection( 1 );       // display the "Unconnected" tab

    // Generate the report
    if( !reportName.IsEmpty() )
    {
        if( writeReport( reportName ) )
        {
            msg.Printf( _( "Report file \"%s\" created" ), GetChars( reportName ) );
            wxMessageDialog popupWindow( this, msg, _( "Disk File Report Completed" ) );
            popupWindow.ShowModal();
        }
        else
        {
            msg.Printf( _( "Unable to create report file \"%s\"" ), GetChars( reportName ) );
            DisplayError( this, msg );
        }
    }

    UpdateDisplayedCounts();

    wxEndBusyCursor();

    /* there is currently nothing visible on the DrawPanel for unconnected pads
     *  RedrawDrawPanel();
     */
}


void DIALOG_DRC_CONTROL::OnButtonBrowseRptFileClick( wxCommandEvent&  )
{
    wxFileName fn = m_brdEditor->GetBoard()->GetFileName();
    fn.SetExt( ReportFileExtension );
    wxString prj_path =  Prj().GetProjectPath();

    wxFileDialog dlg( this, _( "Save DRC Report File" ), prj_path, fn.GetFullName(),
                      ReportFileWildcard(), wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    if( dlg.ShowModal() == wxID_CANCEL )
        return;

    m_CreateRptCtrl->SetValue( true );
    m_RptFilenameCtrl->SetValue( dlg.GetPath() );
}


void DIALOG_DRC_CONTROL::OnCancelClick( wxCommandEvent& event )
{
    SetReturnCode( wxID_CANCEL );
    SetDrcParmeters();

    // The dialog can be modal or not modal.
    // Leave the DRC caller destroy (or not) the dialog
    m_tester->DestroyDRCDialog( wxID_CANCEL );
}


void DIALOG_DRC_CONTROL::OnReportCheckBoxClicked( wxCommandEvent& event )
{
    if( m_CreateRptCtrl->IsChecked() )
        m_RptFilenameCtrl->SetFocus();
}


void DIALOG_DRC_CONTROL::OnReportFilenameEdited( wxCommandEvent &event )
{
    m_CreateRptCtrl->SetValue( event.GetString().Length() );
}


void DIALOG_DRC_CONTROL::OnLeftDClickClearance( wxMouseEvent& event )
{
    event.Skip();

    int selection = m_ClearanceListBox->GetSelection();

    if( selection != wxNOT_FOUND )
    {
        // Find the selected MARKER in the PCB, position cursor there.
        // Then close the dialog.
        const DRC_ITEM* item = m_ClearanceListBox->GetItem( selection );

        if( item )
        {
            auto pos = item->GetPointA();

            if( auto marker = item->GetParent() )
                pos = marker->GetPos();

            // When selecting a item, center it on GAL and just move the graphic
            // cursor in legacy mode gives the best result
            bool center = m_brdEditor->IsGalCanvasActive() ? true : false;
            m_brdEditor->FocusOnLocation( pos, true, center );

            if( !IsModal() )
            {
                // turn control over to m_brdEditor, hide this DIALOG_DRC_CONTROL window,
                // no destruction so we can preserve listbox cursor
                Show( false );

                // We do not want the clarify selection popup when releasing the
                // left button in the main window
                m_brdEditor->SkipNextLeftButtonReleaseEvent();
            }
        }
    }
}


void DIALOG_DRC_CONTROL::OnRightUpUnconnected( wxMouseEvent& event )
{
    // popup menu to go to either of the items listed in the DRC_ITEM.

    int selection = m_UnconnectedListBox->GetSelection();

    if( selection != wxNOT_FOUND )
        doSelectionMenu( m_UnconnectedListBox->GetItem( selection ) );
}


void DIALOG_DRC_CONTROL::OnRightUpClearance( wxMouseEvent& event )
{
    // popup menu to go to either of the items listed in the DRC_ITEM.

    int selection = m_ClearanceListBox->GetSelection();

    if( selection != wxNOT_FOUND )
        doSelectionMenu( m_ClearanceListBox->GetItem( selection ) );
}


void DIALOG_DRC_CONTROL::doSelectionMenu( const DRC_ITEM* aItem )
{
    // popup menu to go to either of the items listed in the DRC_ITEM.
    GENERAL_COLLECTOR items;

    items.Append( aItem->GetMainItem( m_brdEditor->GetBoard() ) );

    if( aItem->HasSecondItem() )
        items.Append( aItem->GetAuxiliaryItem( m_brdEditor->GetBoard() ) );

    BOARD_THAWER thawer( m_brdEditor );
    m_brdEditor->GetToolManager()->RunAction( PCB_ACTIONS::selectionMenu, true, &items );
    m_brdEditor->GetCanvas()->Refresh();
}


void DIALOG_DRC_CONTROL::OnLeftDClickUnconnected( wxMouseEvent& event )
{
    event.Skip();

    int selection = m_UnconnectedListBox->GetSelection();

    if( selection != wxNOT_FOUND )
    {
        // Find the selected DRC_ITEM in the listbox, position cursor there.
        // Then hide the dialog.
        const DRC_ITEM* item = m_UnconnectedListBox->GetItem( selection );
        if( item )
        {
            // When selecting a item, center it on GAL and just move the graphic
            // cursor in legacy mode gives the best result
            bool center = m_brdEditor->IsGalCanvasActive() ? true : false;
            m_brdEditor->FocusOnLocation( item->GetPointA(), true, center );

            if( !IsModal() )
            {
                Show( false );

                // We do not want the clarify selection popup when releasing the
                // left button in the main window
                m_brdEditor->SkipNextLeftButtonReleaseEvent();
            }
        }
    }
}


/* called when switching from Error list to Unconnected list
 * To avoid mistakes, the current marker is selection is cleared
 */
void DIALOG_DRC_CONTROL::OnChangingMarkerList( wxNotebookEvent& event )
{
    // Shouldn't be necessary, but is on at least OSX
    m_Notebook->ChangeSelection( event.GetSelection() );

    m_DeleteCurrentMarkerButton->Enable( false );
    m_ClearanceListBox->SetSelection( -1 );
    m_UnconnectedListBox->SetSelection( -1 );
}


void DIALOG_DRC_CONTROL::OnMarkerSelectionEvent( wxCommandEvent& event )
{
    int selection = event.GetSelection();

    if( selection != wxNOT_FOUND )
    {
        // until a MARKER is selected, this button is not enabled.
        m_DeleteCurrentMarkerButton->Enable( true );

        // Find the selected DRC_ITEM in the listbox, position cursor there.
        const DRC_ITEM* item = m_ClearanceListBox->GetItem( selection );
        if( item )
        {
            auto pos = item->GetPointA();

            if( auto marker = item->GetParent() )
                pos = marker->GetPos();

            // When selecting a item, center it on GAL and just move the graphic
            // cursor in legacy mode gives the best result
            bool center = m_brdEditor->IsGalCanvasActive() ? true : false;
            m_brdEditor->FocusOnLocation( pos, false, center );
            RedrawDrawPanel();
        }
    }

    event.Skip();
}


void DIALOG_DRC_CONTROL::OnUnconnectedSelectionEvent( wxCommandEvent& event )
{
    int selection = event.GetSelection();

    if( selection != wxNOT_FOUND )
    {
        // until a MARKER is selected, this button is not enabled.
        m_DeleteCurrentMarkerButton->Enable( true );

        // Find the selected DRC_ITEM in the listbox, position cursor there.
        const DRC_ITEM* item = m_UnconnectedListBox->GetItem( selection );

        if( item )
        {
            // When selecting a item, center it on GAL and just move the graphic
            // cursor in legacy mode gives the best result
            bool center = m_brdEditor->IsGalCanvasActive() ? true : false;
            m_brdEditor->FocusOnLocation( item->GetPointA(), false, center );
            RedrawDrawPanel();
        }
    }

    event.Skip();
}


void DIALOG_DRC_CONTROL::RedrawDrawPanel()
{
    BOARD_THAWER thawer( m_brdEditor );

    m_brdEditor->GetCanvas()->Refresh();
}


void DIALOG_DRC_CONTROL::DelDRCMarkers()
{
    m_brdEditor->SetCurItem( NULL );           // clear curr item, because it could be a DRC marker

    // Clear current selection list to avoid selection of deleted items
    m_brdEditor->GetToolManager()->RunAction( PCB_ACTIONS::selectionClear, true );

    m_ClearanceListBox->DeleteAllItems();
    m_UnconnectedListBox->DeleteAllItems();
    m_DeleteCurrentMarkerButton->Enable( false );
}


const wxString DIALOG_DRC_CONTROL::makeValidFileNameReport()
{
    wxFileName fn = m_RptFilenameCtrl->GetValue();

    if( !fn.HasExt() )
    {
        fn.SetExt( ReportFileExtension );
        m_RptFilenameCtrl->SetValue( fn.GetFullPath() );
    }

    // Ensure it is an absolute filename. if it is given relative
    // it will be made relative to the project
    if( !fn.IsAbsolute() )
    {
        wxString prj_path =  Prj().GetProjectPath();
        fn.MakeAbsolute( prj_path );
    }

    return fn.GetFullPath();
}


bool DIALOG_DRC_CONTROL::writeReport( const wxString& aFullFileName )
{
    FILE* fp = wxFopen( aFullFileName, wxT( "w" ) );

    if( fp == NULL )
        return false;

    int count;
    EDA_UNITS_T units = GetUserUnits();

    fprintf( fp, "** Drc report for %s **\n",
             TO_UTF8( m_brdEditor->GetBoard()->GetFileName() ) );

    wxDateTime now = wxDateTime::Now();

    fprintf( fp, "** Created on %s **\n", TO_UTF8( now.Format( wxT( "%F %T" ) ) ) );

    count = m_ClearanceListBox->GetItemCount();

    fprintf( fp, "\n** Found %d DRC errors **\n", count );

    for( int i = 0;  i<count;  ++i )
        fprintf( fp, "%s", TO_UTF8( m_ClearanceListBox->GetItem( i )->ShowReport( units ) ) );

    count = m_UnconnectedListBox->GetItemCount();

    fprintf( fp, "\n** Found %d unconnected pads **\n", count );

    for( int i = 0;  i<count;  ++i )
        fprintf( fp, "%s", TO_UTF8( m_UnconnectedListBox->GetItem( i )->ShowReport( units ) ) );

    fprintf( fp, "\n** End of Report **\n" );

    fclose( fp );

    return true;
}


void DIALOG_DRC_CONTROL::OnDeleteOneClick( wxCommandEvent& event )
{
    int selectedIndex;
    int curTab = m_Notebook->GetSelection();

    if( curTab == 0 )
    {
        selectedIndex = m_ClearanceListBox->GetSelection();

        if( selectedIndex != wxNOT_FOUND )
        {
            m_ClearanceListBox->DeleteItem( selectedIndex );

            // redraw the pcb
            RedrawDrawPanel();
        }
    }
    else if( curTab == 1 )
    {
        selectedIndex = m_UnconnectedListBox->GetSelection();

        if( selectedIndex != wxNOT_FOUND )
        {
            m_UnconnectedListBox->DeleteItem( selectedIndex );

            /* these unconnected DRC_ITEMs are not currently visible on the pcb
             *  RedrawDrawPanel();
             */
        }
    }

    UpdateDisplayedCounts();
}


void DIALOG_DRC_CONTROL::UpdateDisplayedCounts()
{
    int marker_count = m_ClearanceListBox->GetItemCount();
    int unconnected_count = m_UnconnectedListBox->GetItemCount();

    m_Notebook->SetPageText( 0, wxString::Format( m_markersTitleTemplate, marker_count ) );
    m_Notebook->SetPageText( 1, wxString::Format( m_unconnectedTitleTemplate, unconnected_count ) );

}
