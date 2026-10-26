/*****************************************************************************
 * InfoWindow.cpp: media information of the modern Haiku interface
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
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

#include <Button.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <String.h>
#include <TabView.h>
#include <TextControl.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_input_item.h>
#include <vlc_meta.h>

#include "InfoWindow.h"

static const vlc_meta_type_t kFields[INFO_FIELDS] = {
    vlc_meta_Title, vlc_meta_Artist, vlc_meta_Album, vlc_meta_Genre,
    vlc_meta_Date, vlc_meta_TrackNumber, vlc_meta_Copyright,
    vlc_meta_Publisher, vlc_meta_Description, vlc_meta_URL,
};

InfoWindow::InfoWindow( intf_thread_t * _p_intf, BRect frame )
    : BWindow( frame, _("Media Information"), B_TITLED_WINDOW_LOOK,
               B_NORMAL_WINDOW_FEEL,
               B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS ),
      p_intf( _p_intf )
{
    float spacing = be_control_look->DefaultItemSpacing();

    /* General: the metadata, and where the media is */
    BView * general = new BView( _("General"), 0 );
    general->SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
    BGridLayout * grid;
    BLayoutBuilder::Grid<>( general, spacing / 2, spacing / 2 )
        .GetLayout( &grid )
        .SetInsets( spacing );
    for( int i = 0; i < INFO_FIELDS; i++ )
    {
        const char * psz_label = i < INFO_FIELDS - 1
                               ? vlc_meta_TypeToLocalizedString( kFields[i] )
                               : _("Location");
        BString label( psz_label );
        label << ":";
        fFields[i] = new BTextControl( label.String(), "", NULL );
        fFields[i]->TextView()->MakeEditable( false );
        grid->AddItem( fFields[i]->CreateLabelLayoutItem(), 0, i );
        grid->AddItem( fFields[i]->CreateTextViewLayoutItem(), 1, i );
    }
    grid->SetColumnWeight( 1, 1.0f );
    grid->AddItem( BSpaceLayoutItem::CreateGlue(), 0, INFO_FIELDS, 2 );

    /* Codec: the streams, as the demuxer and decoders describe them */
    fCodecs = new BOutlineListView( "codecs", B_SINGLE_SELECTION_LIST );
    BScrollView * scroll = new BScrollView( _("Codec"), fCodecs, 0, false, true );

    fTabs = new BTabView( "tabs", B_WIDTH_FROM_LABEL );
    fTabs->AddTab( general );
    fTabs->AddTab( scroll );
    fTabs->SetBorder( B_NO_BORDER );

    BButton * close = new BButton( "close", _("Close"),
                                   new BMessage( B_QUIT_REQUESTED ) );
    BLayoutBuilder::Group<>( this, B_VERTICAL, 0 )
        .Add( fTabs )
        .AddGroup( B_HORIZONTAL )
            .SetInsets( spacing )
            .AddGlue()
            .Add( close )
        .End();

    /* Start the window thread, hidden */
    Hide();
    Show();
}

bool InfoWindow::QuitRequested()
{
    Hide();
    return false;
}

void InfoWindow::MessageReceived( BMessage * message )
{
    BWindow::MessageReceived( message );
}

void InfoWindow::ShowTab( int32 tab )
{
    fTabs->Select( tab );
}

void InfoWindow::Update()
{
    playlist_t * p_playlist = pl_Get( p_intf );
    input_item_t * p_item = NULL;

    PL_LOCK;
    playlist_item_t * p_current = playlist_CurrentPlayingItem( p_playlist );
    if( p_current && p_current->p_input )
        p_item = input_item_Hold( p_current->p_input );
    PL_UNLOCK;

    for( int i = 0; i < INFO_FIELDS; i++ )
    {
        char * psz = NULL;
        if( p_item )
            psz = i < INFO_FIELDS - 1 ? input_item_GetMeta( p_item, kFields[i] )
                                      : input_item_GetURI( p_item );
        if( i == 0 && p_item && ( !psz || !*psz ) )
        {
            free( psz );
            psz = input_item_GetTitleFbName( p_item );
        }
        if( strcmp( fFields[i]->Text(), psz ? psz : "" ) )
            fFields[i]->SetText( psz ? psz : "" );
        free( psz );
    }

    /* Rebuilt only when it changed, so that it doesn't jump around */
    BOutlineListView * list = fCodecs;
    BString all;
    BList items;
    if( p_item )
    {
        vlc_mutex_lock( &p_item->lock );
        for( int i = 0; i < p_item->i_categories; i++ )
        {
            info_category_t * p_cat = p_item->pp_categories[i];
            BStringItem * category = new BStringItem( p_cat->psz_name );
            items.AddItem( category );
            all << p_cat->psz_name << "\n";
            for( int j = 0; j < p_cat->i_infos; j++ )
            {
                BString text;
                text << p_cat->pp_infos[j]->psz_name << ": "
                     << p_cat->pp_infos[j]->psz_value;
                items.AddItem( new BStringItem( text.String(), 1 ) );
                all << text << "\n";
            }
        }
        vlc_mutex_unlock( &p_item->lock );
        input_item_Release( p_item );
    }

    BString previous;
    for( int32 i = 0; i < list->FullListCountItems(); i++ )
        previous << ( (BStringItem *) list->FullListItemAt( i ) )->Text() << "\n";
    if( previous == all )
    {
        for( int32 i = 0; i < items.CountItems(); i++ )
            delete (BListItem *) items.ItemAt( i );
        return;
    }

    while( list->FullListCountItems() > 0 )
        delete list->RemoveItem( list->FullListCountItems() - 1 );
    BListItem * super = NULL;
    for( int32 i = 0; i < items.CountItems(); i++ )
    {
        BListItem * item = (BListItem *) items.ItemAt( i );
        if( item->OutlineLevel() == 0 )
        {
            list->AddItem( item );
            super = item;
        }
        else
            list->AddUnder( item, super );
    }
}

void InfoWindow::ReallyQuit()
{
    /* Wait until the window is gone: with the Haiku API on Linux, Quit()
     * from another thread (and wait_for_thread()) can return before it is */
    BMessenger messenger( this );
    Lock();
    Quit();
    for( int i = 0; i < 200 && messenger.IsValid(); i++ )
        snooze( 10000 );
}
