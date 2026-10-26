/*****************************************************************************
 * PreferencesWindow.cpp: Haiku interface preferences window
 *****************************************************************************
 * Copyright (C) 1999, 2000, 2001 the VideoLAN team
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Authors: Eric Petit <titer@m0k.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*
 * Like the BeOS preferences window of VLC 0.x, this is built from the
 * modules' own configuration: a tree of categories, subcategories and
 * plugins, and for each one a panel with a control per option.
 */

#include <InterfaceKit.h>
#include <SupportKit.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>
#include <vlc_config_cat.h>
#include <vlc_charset.h>

#include "PreferencesWindow.h"

#define TYPE_CATEGORY    0
#define TYPE_SUBCATEGORY 2
#define TYPE_MODULE      3

#define ROW_HEIGHT       25

/* The general subcategories are shown on their category's own panel,
 * as in the Qt interface */
static bool IsGeneralSubcategory( int id )
{
    return id == SUBCAT_VIDEO_GENERAL || id == SUBCAT_ADVANCED_MISC
        || id == SUBCAT_INPUT_GENERAL || id == SUBCAT_INTERFACE_GENERAL
        || id == SUBCAT_SOUT_GENERAL || id == SUBCAT_PLAYLIST_GENERAL
        || id == SUBCAT_AUDIO_GENERAL;
}

/*****************************************************************************
 * PreferencesWindow::PreferencesWindow
 *****************************************************************************/
PreferencesWindow::PreferencesWindow( intf_thread_t * _p_intf,
                                      BRect frame, const char * name )
    : BWindow( frame, name, B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
               B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS )
{
    p_intf   = _p_intf;
    fCurrent = NULL;

    BRect rect;

    SetSizeLimits( PREFS_WINDOW_WIDTH, 2000, PREFS_WINDOW_HEIGHT, 2000 );

    /* The "background" view */
    fPrefsView = new BView( Bounds(), NULL, B_FOLLOW_ALL, B_WILL_DRAW );
    fPrefsView->SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
    AddChild( fPrefsView );

    /* Create a scrollable outline view for the preferences tree */
    rect = Bounds();
    rect.InsetBy( 10, 10 );
    rect.right = rect.left + 170;
    fOutline = new BOutlineListView( rect, "preferences tree",
                                     B_SINGLE_SELECTION_LIST,
                                     B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM );
    BScrollView * scrollview =
        new BScrollView( "scrollview", fOutline,
                         B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM,
                         0, false, true );
    fPrefsView->AddChild( scrollview );

    /* We need to be informed if the user selects an item */
    fOutline->SetSelectionMessage( new BMessage( PREFS_ITEM_SELECTED ) );

    /* Create a dummy, empty view so we can correctly place the real
       config views later */
    rect.bottom -= 40;
    rect.left = rect.right + 15 + B_V_SCROLL_BAR_WIDTH;
    rect.right = Bounds().right - 15;
    fDummyView = new BView( rect, "", B_FOLLOW_ALL_SIDES, B_WILL_DRAW );
    fDummyView->SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
    fPrefsView->AddChild( fDummyView );

    /* Build the category tree from the core module */
    ConfigItem * catItem = NULL, * subcatItem, * otherItem;
    unsigned confsize;
    module_config_t * p_config = module_config_get( module_get_main(), &confsize );

    for( unsigned i = 0; i < confsize; i++ )
    {
        const module_config_t * p_item = p_config + i;

        switch( p_item->i_type )
        {
            case CONFIG_CATEGORY:
                if( p_item->value.i == -1 )
                    break;
                catItem = new ConfigItem( p_intf,
                    config_CategoryNameGet( p_item->value.i ),
                    TYPE_CATEGORY, p_item->value.i, NULL,
                    config_CategoryHelpGet( p_item->value.i ) );
                fOutline->AddItem( catItem );
                break;

            case CONFIG_SUBCATEGORY:
                if( p_item->value.i == -1 )
                    break;
                if( !catItem )
                {
                    msg_Warn( p_intf, "subcategory without a category" );
                    break;
                }
                if( IsGeneralSubcategory( p_item->value.i ) )
                {
                    catItem->SetGeneralSubcategory( p_item->value.i );
                    break;
                }
                subcatItem = new ConfigItem( p_intf,
                    config_CategoryNameGet( p_item->value.i ),
                    TYPE_SUBCATEGORY, p_item->value.i, NULL,
                    config_CategoryHelpGet( p_item->value.i ) );
                fOutline->AddUnder( subcatItem, catItem );
                break;
        }
    }
    module_config_free( p_config );

    /* Now parse all other modules */
    size_t count;
    module_t ** p_list = module_list_get( &count );

    for( size_t i = 0; i < count; i++ )
    {
        module_t * p_module = p_list[i];
        int category = -1, subcategory = -1;
        bool options = false;

        if( module_is_main( p_module ) )
            continue;

        p_config = module_config_get( p_module, &confsize );
        for( unsigned j = 0; j < confsize; j++ )
        {
            const module_config_t * p_item = p_config + j;

            if( p_item->i_type == CONFIG_CATEGORY )
                category = p_item->value.i;
            else if( p_item->i_type == CONFIG_SUBCATEGORY )
                subcategory = p_item->value.i;
            else if( CONFIG_ITEM( p_item->i_type ) )
                options = true;

            if( options && category >= 0 && subcategory >= 0 )
                break;
        }
        module_config_free( p_config );

        if( !options || category < 0 || subcategory < 0 )
            continue;

        catItem = NULL;
        for( int j = 0; j < fOutline->CountItemsUnder( NULL, true ); j++ )
        {
            catItem = (ConfigItem*) fOutline->ItemUnderAt( NULL, true, j );
            if( catItem->ObjectId() == category )
                break;
            catItem = NULL;
        }
        if( !catItem )
            continue;

        subcatItem = NULL;
        for( int j = 0; j < fOutline->CountItemsUnder( catItem, true ); j++ )
        {
            subcatItem = (ConfigItem*) fOutline->ItemUnderAt( catItem, true, j );
            if( subcatItem->Type() == TYPE_SUBCATEGORY
             && subcatItem->ObjectId() == subcategory )
                break;
            subcatItem = NULL;
        }
        if( !subcatItem )
            subcatItem = catItem;

        otherItem = new ConfigItem( p_intf,
            module_get_name( p_module, false ), TYPE_MODULE, 0,
            module_get_object( p_module ), NULL );
        fOutline->AddUnder( otherItem, subcatItem );
    }
    module_list_free( p_list );

    /* Collapse the whole tree */
    for( int i = 0; i < fOutline->FullListCountItems(); i++ )
    {
        otherItem = (ConfigItem *) fOutline->FullListItemAt( i );
        fOutline->Collapse( otherItem );
    }

    /* Add the buttons */
    BButton * button;
    rect = Bounds();
    rect.InsetBy( 10, 10 );
    rect.left = rect.right - 80;
    rect.top = rect.bottom - 25;
    button = new BButton( rect, "", _("Apply"), new BMessage( PREFS_APPLY ),
                          B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM );
    fPrefsView->AddChild( button );
    rect.OffsetBy( -90, 0 );
    button = new BButton( rect, "", _("Save"), new BMessage( PREFS_SAVE ),
                          B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM );
    button->MakeDefault( true );
    fPrefsView->AddChild( button );
    rect.OffsetBy( -90, 0 );
    button = new BButton( rect, "", _("Defaults"),
                          new BMessage( PREFS_DEFAULTS ),
                          B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM );
    fPrefsView->AddChild( button );

    /* Select the first item */
    fOutline->Select( 0 );

    /* start window thread in hidden state */
    Hide();
    Show();
}

/*****************************************************************************
 * PreferencesWindow::~PreferencesWindow
 *****************************************************************************/
PreferencesWindow::~PreferencesWindow()
{
    /* The panel on screen goes with the window; the items delete the
     * others */
    for( int i = 0; i < fOutline->FullListCountItems(); i++ )
        delete (ConfigItem *) fOutline->FullListItemAt( i );
}

/*****************************************************************************
 * PreferencesWindow::QuitRequested
 *****************************************************************************/
bool PreferencesWindow::QuitRequested()
{
    if( !IsHidden() )
    {
        Hide();
    }
    return false;
}

/*****************************************************************************
 * PreferencesWindow::MessageReceived
 *****************************************************************************/
void PreferencesWindow::MessageReceived( BMessage * message )
{
    switch( message->what )
    {
        case PREFS_ITEM_SELECTED:
            Update();
            break;

        case PREFS_DEFAULTS:
        {
            BAlert * alert = new BAlert( _("Defaults"),
                _("Reset all settings to their default values?"),
                _("Cancel"), _("Reset"), NULL, B_WIDTH_AS_USUAL,
                B_WARNING_ALERT );
            alert->SetShortcut( 0, B_ESCAPE );
            if( alert->Go() == 1 )
            {
                config_ResetAll( VLC_OBJECT( p_intf ) );
                config_SaveConfigFile( VLC_OBJECT( p_intf ) );
                Apply( false );
            }
            break;
        }

        case PREFS_APPLY:
            Apply( true );
            break;

        case PREFS_SAVE:
            Apply( true );
            config_SaveConfigFile( VLC_OBJECT( p_intf ) );
            break;

        default:
            BWindow::MessageReceived( message );
    }
}

/*****************************************************************************
 * PreferencesWindow::FrameResized
 *****************************************************************************/
void PreferencesWindow::FrameResized( float width, float height )
{
    BWindow::FrameResized( width, height );
    if( fCurrent )
        fCurrent->UpdateScrollBar();
}

/*****************************************************************************
 * PreferencesWindow::Update
 *****************************************************************************/
void PreferencesWindow::Update()
{
    /* Get the selected item, if any */
    if( fOutline->CurrentSelection() < 0 )
        return;

    /* Detach the old box if any */
    if( fCurrent )
    {
        fCurrent->ResetScroll();
        fDummyView->RemoveChild( fCurrent->Box() );
    }

    /* Add the new one... */
    fCurrent = (ConfigItem *)
        fOutline->ItemAt( fOutline->CurrentSelection() );
    fDummyView->AddChild( fCurrent->Box( fDummyView->Bounds() ) );

    /* ...then resize it (we must resize it after it's attached or the
       children don't get adjusted) */
    fCurrent->Box()->ResizeTo( fDummyView->Bounds().Width(),
                               fDummyView->Bounds().Height() );
    fCurrent->UpdateScrollBar();
}

/*****************************************************************************
 * PreferencesWindow::Apply
 * Apply changes if doIt is true, revert them otherwise
 *****************************************************************************/
void PreferencesWindow::Apply( bool doIt )
{
    ConfigItem * item;

    for( int i = 0; i < fOutline->FullListCountItems(); i++ )
    {
        item = (ConfigItem*) fOutline->FullListItemAt( i );
        item->Apply( doIt );
    }
}

/*****************************************************************************
 * PreferencesWindow::ReallyQuit
 *****************************************************************************/
void
PreferencesWindow::ReallyQuit()
{
    /* Wait until the window is gone: with the Haiku API on Linux, Quit()
     * from another thread (and wait_for_thread()) can return before it is */
    BMessenger messenger( this );
    Lock();
    Hide();
    Quit();
    /* The looper's port goes away at the end of its destructor */
    for( int i = 0; i < 200 && messenger.IsValid(); i++ )
        snooze( 10000 );
}

/***********************************************************************
 * ConfigItem::ConfigItem
 **********************************************************************/
ConfigItem::ConfigItem( intf_thread_t * _p_intf, const char * name,
                        int type, int objectId, const char * module,
                        const char * help )
    : BStringItem( name ? _(name) : "" )
{
    p_intf     = _p_intf;
    fType      = type;
    fObjectId  = objectId;
    fGeneralId = -1;
    fModule    = module ? strdup( module ) : NULL;
    fHelp      = help ? strdup( help ) : NULL;

    fBox    = NULL;
    fScroll = NULL;
    fView   = NULL;
}

/***********************************************************************
 * ConfigItem::~ConfigItem
 **********************************************************************/
ConfigItem::~ConfigItem()
{
    free( fModule );
    free( fHelp );
    if( fBox && !fBox->Parent() )
        delete fBox;
}

/***********************************************************************
 * ConfigItem::_AddWidgets: adds a control for each option, from the
 * given subcategory only if subcategory >= 0
 **********************************************************************/
void ConfigItem::_AddWidgets( BRect & r, const module_config_t * conf,
                              unsigned size, int subcategory )
{
    bool inside = subcategory < 0;

    for( unsigned i = 0; i < size; i++ )
    {
        const module_config_t * p_item = conf + i;

        if( p_item->i_type == CONFIG_CATEGORY
         || p_item->i_type == CONFIG_SUBCATEGORY )
        {
            if( subcategory >= 0 )
            {
                if( p_item->i_type == CONFIG_SUBCATEGORY
                 && p_item->value.i == subcategory )
                    inside = true;
                else if( inside )
                    break;
            }
            continue;
        }
        if( !inside )
            continue;

        if( p_item->i_type == CONFIG_SECTION )
        {
            /* A heading for the options that follow */
            BRect sr( r.left, r.top + 6, r.right, r.top + ROW_HEIGHT );
            BStringView * section = new BStringView( sr, NULL,
                p_item->psz_text ? _(p_item->psz_text) : "",
                B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP );
            section->SetFont( be_bold_font );
            fView->AddChild( section );
            r.top += ROW_HEIGHT;
            continue;
        }

        ConfigWidget * widget = new ConfigWidget( p_intf, r, p_item );
        if( !widget->InitCheck() )
        {
            delete widget;
            continue;
        }
        fView->AddChild( widget );
        widget->Apply( false );
        r.top += widget->Bounds().Height() + 1;
    }
}

/***********************************************************************
 * ConfigItem::Box: builds the panel the first time it is needed
 **********************************************************************/
BBox * ConfigItem::Box( BRect frame )
{
    if( fBox )
        return fBox;

    /* Built at its final size, so that texts wrap at the right width */
    BRect r( 0, 0, frame.Width(), frame.Height() );
    fBox = new BBox( r, NULL, B_FOLLOW_ALL );
    fBox->SetLabel( Text() );

    r = BRect( 10, 20, fBox->Bounds().right - B_V_SCROLL_BAR_WIDTH - 10,
               fBox->Bounds().bottom - 10 );
    fView = new BView( r, NULL, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
                       B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE );
    fView->SetViewUIColor( B_PANEL_BACKGROUND_COLOR );

    r = fView->Bounds();
    r.InsetBy( 10, 10 );

    /* Categories and subcategories start with their description */
    if( fHelp && *fHelp )
    {
        BRect tr( r.left, r.top, r.right, r.top + 60 );
        BTextView * text = new BTextView( tr, NULL,
            BRect( 0, 0, tr.Width(), tr.Height() ),
            B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP, B_WILL_DRAW );
        text->SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
        text->MakeEditable( false );
        text->MakeSelectable( false );
        text->SetWordWrap( true );
        text->SetText( _(fHelp) );
        fView->AddChild( text );
        text->ResizeTo( tr.Width(), text->TextHeight( 0, text->TextLength() ) + 4 );
        r.top += text->Bounds().Height() + 10;
    }

    unsigned confsize;
    module_config_t * p_config;

    switch( fType )
    {
        case TYPE_CATEGORY:
            if( fGeneralId >= 0 )
            {
                p_config = module_config_get( module_get_main(), &confsize );
                _AddWidgets( r, p_config, confsize, fGeneralId );
                module_config_free( p_config );
            }
            break;

        case TYPE_SUBCATEGORY:
            p_config = module_config_get( module_get_main(), &confsize );
            _AddWidgets( r, p_config, confsize, fObjectId );
            module_config_free( p_config );
            break;

        case TYPE_MODULE:
        {
            module_t * p_module = module_find( fModule );
            if( p_module )
            {
                const char * psz_help = module_get_help( p_module );
                if( psz_help && *psz_help )
                {
                    BRect tr( r.left, r.top, r.right, r.top + 60 );
                    BTextView * text = new BTextView( tr, NULL,
                        BRect( 0, 0, tr.Width(), tr.Height() ),
                        B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP, B_WILL_DRAW );
                    text->SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
                    text->MakeEditable( false );
                    text->MakeSelectable( false );
                    text->SetText( module_gettext( p_module, psz_help ) );
                    fView->AddChild( text );
                    text->ResizeTo( tr.Width(),
                        text->TextHeight( 0, text->TextLength() ) + 4 );
                    r.top += text->Bounds().Height() + 10;
                }
                p_config = module_config_get( p_module, &confsize );
                _AddWidgets( r, p_config, confsize, -1 );
                module_config_free( p_config );
            }
            break;
        }
    }

    /* Create a scroll view around our fView */
    fScroll = new BScrollView( NULL, fView, B_FOLLOW_ALL, 0, false,
                               true, B_FANCY_BORDER );
    fScroll->SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
    fBox->AddChild( fScroll );

    /* Adjust fView's height to the size it actually needs (we do this
       only now so the BScrollView fits the BBox) */
    fView->ResizeTo( fView->Bounds().Width(), r.top + 10 );

    return fBox;
}

/*****************************************************************************
 * ConfigItem::UpdateScrollBar
 *****************************************************************************/
void ConfigItem::UpdateScrollBar()
{
    /* We have to fix the scrollbar manually because it doesn't handle
       correctly simple BViews */

    if( !fScroll )
    {
        return;
    }

    /* Get the available BRect for display */
    BRect display = fScroll->Bounds();
    display.right -= B_V_SCROLL_BAR_WIDTH;

    /* Fix the scrollbar */
    BScrollBar * scrollBar;
    BRect visible = display & fView->Bounds();
    BRect total   = display | fView->Bounds();
    scrollBar = fScroll->ScrollBar( B_VERTICAL );
    long max = (long)( fView->Bounds().Height() - visible.Height() );
    if( max < 0 ) max = 0;
    scrollBar->SetRange( 0, max );
    scrollBar->SetProportion( visible.Height() / total.Height() );
    scrollBar->SetSteps( 10, 100 );

    fScroll->Invalidate();
    fView->Invalidate();
}

/*****************************************************************************
 * ConfigItem::ResetScroll
 *****************************************************************************/
void ConfigItem::ResetScroll()
{
    if( !fScroll )
    {
        return;
    }

    fView->ScrollTo( 0, 0 );
}

/***********************************************************************
 * ConfigItem::Apply
 **********************************************************************/
void ConfigItem::Apply( bool doIt )
{
    if( !fView )
    {
        return;
    }

    /* Call ConfigWidget::Apply for every option on the panel */
    for( int i = 0; i < fView->CountChildren(); i++ )
    {
        ConfigWidget * widget = dynamic_cast<ConfigWidget*>( fView->ChildAt( i ) );
        if( widget )
            widget->Apply( doIt );
    }
}

/***********************************************************************
 * ConfigWidget::ConfigWidget
 ***********************************************************************
 * Builds a view with the right controls for the given config variable.
 *  rect: the BRect where we place ourselves. All we care is its width
 *    and its top coordinate, since we adapt our height to take only
 *    the place we need
 **********************************************************************/
ConfigWidget::ConfigWidget( intf_thread_t * _p_intf, BRect rect,
                            const module_config_t * p_item )
    : BView( rect, NULL, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
             B_WILL_DRAW | B_FRAME_EVENTS )
{
    p_intf = _p_intf;

    SetViewUIColor( B_PANEL_BACKGROUND_COLOR );

    fInitOK      = true;
    fType        = p_item->i_type;
    fName        = strdup( p_item->psz_name ? p_item->psz_name : "" );
    fTextControl = NULL;
    fCheckBox    = NULL;
    fPopUpMenu   = NULL;
    fMenuField   = NULL;

    const char * psz_text = p_item->psz_text ? _(p_item->psz_text) : fName;
    ResizeTo( Bounds().Width(), ROW_HEIGHT - 1 );

    switch( fType )
    {
        case CONFIG_ITEM_BOOL:
            fCheckBox = new BCheckBox( Bounds(), NULL, psz_text,
                new BMessage(), B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP );
            AddChild( fCheckBox );
            break;

        case CONFIG_ITEM_KEY:
            /* Global hotkeys need a module Haiku doesn't have */
            if( !strncmp( fName, "global-", 7 ) )
            {
                fInitOK = false;
                break;
            }
            /* fall through */
        case CONFIG_ITEM_MODULE:
        case CONFIG_ITEM_MODULE_CAT:
        case CONFIG_ITEM_MODULE_LIST:
        case CONFIG_ITEM_MODULE_LIST_CAT:
        case CONFIG_ITEM_STRING:
        case CONFIG_ITEM_PASSWORD:
        case CONFIG_ITEM_LOADFILE:
        case CONFIG_ITEM_SAVEFILE:
        case CONFIG_ITEM_DIRECTORY:
        case CONFIG_ITEM_FONT:
        case CONFIG_ITEM_INTEGER:
        case CONFIG_ITEM_RGB:
        case CONFIG_ITEM_FLOAT:
            _BuildMenu( p_item );
            if( fPopUpMenu )
            {
                fMenuField = new BMenuField( Bounds(), NULL, psz_text,
                    fPopUpMenu, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP );
                fMenuField->SetDivider( Bounds().Width() / 2 );
                AddChild( fMenuField );
            }
            else
            {
                fTextControl = new BTextControl( Bounds(), NULL, psz_text,
                    NULL, new BMessage(), B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP );
                fTextControl->SetDivider( Bounds().Width() / 2 );
                if( fType == CONFIG_ITEM_PASSWORD )
                    fTextControl->TextView()->HideTyping( true );
                AddChild( fTextControl );
            }
            break;

        default:
            fInitOK = false;
            break;
    }

    if( fInitOK && p_item->psz_longtext )
        SetToolTip( _(p_item->psz_longtext) );
}

ConfigWidget::~ConfigWidget()
{
    free( fName );
}

/* Labels take the left half, controls the right half */
void ConfigWidget::FrameResized( float width, float height )
{
    BView::FrameResized( width, height );
    if( fTextControl )
        fTextControl->SetDivider( width / 2 );
    if( fMenuField )
        fMenuField->SetDivider( width / 2 );
}

/***********************************************************************
 * ConfigWidget::_BuildMenu: a pop-up menu for options with a fixed
 * set of values; none for the others
 **********************************************************************/
void ConfigWidget::_BuildMenu( const module_config_t * p_item )
{
    vlc_object_t * obj = VLC_OBJECT( p_intf );

    if( fType == CONFIG_ITEM_MODULE_CAT )
    {
        /* Modules of the subcategory stored in min */
        fPopUpMenu = new BPopUpMenu( "" );
        BMessage * msg = new BMessage();
        msg->AddString( "value", "" );
        fPopUpMenu->AddItem( new BMenuItem( _("Default"), msg ) );

        size_t count;
        module_t ** p_list = module_list_get( &count );
        for( size_t i = 0; i < count; i++ )
        {
            if( module_is_main( p_list[i] ) )
                continue;
            unsigned confsize;
            module_config_t * p_config = module_config_get( p_list[i], &confsize );
            for( unsigned j = 0; j < confsize; j++ )
            {
                if( p_config[j].i_type == CONFIG_SUBCATEGORY
                 && p_config[j].value.i == p_item->min.i )
                {
                    msg = new BMessage();
                    msg->AddString( "value", module_get_object( p_list[i] ) );
                    fPopUpMenu->AddItem( new BMenuItem(
                        module_get_name( p_list[i], true ), msg ) );
                    break;
                }
            }
            module_config_free( p_config );
        }
        module_list_free( p_list );
        return;
    }

    if( fType == CONFIG_ITEM_INTEGER )
    {
        int64_t * values;
        char ** texts;
        ssize_t count = config_GetIntChoices( obj, fName, &values, &texts );
        if( count <= 0 )
            return;
        fPopUpMenu = new BPopUpMenu( "" );
        for( ssize_t i = 0; i < count; i++ )
        {
            BMessage * msg = new BMessage();
            msg->AddInt64( "value", values[i] );
            fPopUpMenu->AddItem( new BMenuItem(
                texts[i] ? _(texts[i]) : "", msg ) );
            free( texts[i] );
        }
        free( values );
        free( texts );
        return;
    }

    if( fType == CONFIG_ITEM_MODULE || fType == CONFIG_ITEM_STRING )
    {
        char ** values;
        char ** texts;
        ssize_t count = config_GetPszChoices( obj, fName, &values, &texts );
        if( count <= 0 )
            return;
        fPopUpMenu = new BPopUpMenu( "" );
        for( ssize_t i = 0; i < count; i++ )
        {
            BMessage * msg = new BMessage();
            msg->AddString( "value", values[i] ? values[i] : "" );
            const char * psz_label = texts[i] && *texts[i] ? texts[i]
                                   : ( values[i] ? values[i] : "" );
            fPopUpMenu->AddItem( new BMenuItem( _(psz_label), msg ) );
            free( values[i] );
            free( texts[i] );
        }
        free( values );
        free( texts );
    }
}

/***********************************************************************
 * ConfigWidget::Apply
 **********************************************************************/
void ConfigWidget::Apply( bool doIt )
{
    vlc_object_t * obj = VLC_OBJECT( p_intf );
    char string[64];

    if( fCheckBox )
    {
        if( doIt )
            config_PutInt( obj, fName, fCheckBox->Value() ? 1 : 0 );
        else
            fCheckBox->SetValue( config_GetInt( obj, fName ) ? B_CONTROL_ON
                                                             : B_CONTROL_OFF );
        return;
    }

    if( fPopUpMenu )
    {
        if( doIt )
        {
            BMenuItem * item = fPopUpMenu->FindMarked();
            if( !item )
                return;
            BMessage * msg = item->Message();
            if( fType == CONFIG_ITEM_INTEGER )
                config_PutInt( obj, fName, msg->GetInt64( "value", 0 ) );
            else
            {
                const char * psz = msg->GetString( "value", "" );
                config_PutPsz( obj, fName, *psz ? psz : NULL );
            }
            return;
        }

        /* Mark the item with the current value */
        BMenuItem * match = NULL;
        if( fType == CONFIG_ITEM_INTEGER )
        {
            int64_t value = config_GetInt( obj, fName );
            for( int32 i = 0; i < fPopUpMenu->CountItems(); i++ )
                if( fPopUpMenu->ItemAt( i )->Message()->GetInt64( "value", 0 ) == value )
                    match = fPopUpMenu->ItemAt( i );
        }
        else
        {
            char * psz = config_GetPsz( obj, fName );
            const char * value = psz ? psz : "";
            for( int32 i = 0; i < fPopUpMenu->CountItems(); i++ )
                if( !strcmp( fPopUpMenu->ItemAt( i )->Message()->GetString( "value", "" ),
                             value ) )
                    match = fPopUpMenu->ItemAt( i );
            /* No module chosen: that's "any" in module lists */
            if( !match && !*value )
                for( int32 i = 0; i < fPopUpMenu->CountItems(); i++ )
                    if( !strcmp( fPopUpMenu->ItemAt( i )->Message()->GetString( "value", "" ),
                                 "any" ) )
                        match = fPopUpMenu->ItemAt( i );
            free( psz );
        }
        if( match )
            match->SetMarked( true );
        return;
    }

    if( !fTextControl )
        return;

    switch( fType )
    {
        case CONFIG_ITEM_INTEGER:
            if( doIt )
                config_PutInt( obj, fName, strtoll( fTextControl->Text(), NULL, 10 ) );
            else
            {
                snprintf( string, sizeof( string ), "%" PRId64,
                          config_GetInt( obj, fName ) );
                fTextControl->SetText( string );
            }
            break;

        case CONFIG_ITEM_RGB:
            if( doIt )
                config_PutInt( obj, fName, strtoll( fTextControl->Text(), NULL, 16 ) );
            else
            {
                snprintf( string, sizeof( string ), "%06" PRIX64,
                          config_GetInt( obj, fName ) );
                fTextControl->SetText( string );
            }
            break;

        case CONFIG_ITEM_FLOAT:
            if( doIt )
                config_PutFloat( obj, fName, us_atof( fTextControl->Text() ) );
            else
            {
                snprintf( string, sizeof( string ), "%g",
                          config_GetFloat( obj, fName ) );
                fTextControl->SetText( string );
            }
            break;

        default:
            if( doIt )
            {
                const char * psz = fTextControl->Text();
                config_PutPsz( obj, fName, *psz ? psz : NULL );
            }
            else
            {
                char * psz = config_GetPsz( obj, fName );
                fTextControl->SetText( psz ? psz : "" );
                free( psz );
            }
            break;
    }
}
