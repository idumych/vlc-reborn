/*****************************************************************************
 * PreferencesWindow.h
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

#ifndef HAIKU_PREFERENCES_WINDOW_H
#define HAIKU_PREFERENCES_WINDOW_H

#include <InterfaceKit.h>

#define PREFS_WINDOW_WIDTH   700
#define PREFS_WINDOW_HEIGHT  400
#define PREFS_ITEM_SELECTED  'pris'
#define PREFS_DEFAULTS       'prde'
#define PREFS_APPLY          'prap'
#define PREFS_SAVE           'prsa'

/* One option: a label and the control that fits its type */
class ConfigWidget : public BView
{
    public:
                        ConfigWidget( intf_thread_t * p_intf, BRect rect,
                                      const module_config_t * p_item );
                        ~ConfigWidget();
        bool            InitCheck() { return fInitOK; }
        virtual void    FrameResized( float width, float height );
        /* Stores the control's value if doIt, shows the current one
         * otherwise */
        void            Apply( bool doIt );

    private:
        void            _BuildMenu( const module_config_t * p_item );

        intf_thread_t * p_intf;

        bool            fInitOK;
        int             fType;
        char          * fName;

        BTextControl  * fTextControl;
        BCheckBox     * fCheckBox;
        BPopUpMenu    * fPopUpMenu;
        BMenuField    * fMenuField;
};

/* An entry of the tree: a category, a subcategory or a plugin. Its panel
 * is built the first time it is shown. */
class ConfigItem : public BStringItem
{
    public:
                      ConfigItem( intf_thread_t * p_intf,
                                  const char * name, int type,
                                  int objectId, const char * module,
                                  const char * help );
                      ~ConfigItem();
        int           Type() { return fType; }
        int           ObjectId() { return fObjectId; }
        void          SetGeneralSubcategory( int id ) { fGeneralId = id; }
        BBox        * Box( BRect frame = BRect( 0, 0, 100, 100 ) );
        bool          HasBox() { return fBox != NULL; }
        void          UpdateScrollBar();
        void          ResetScroll();
        void          Apply( bool doIt );

    private:
        void          _AddWidgets( BRect & r, const module_config_t * conf,
                                   unsigned size, int subcategory );

        intf_thread_t * p_intf;

        int             fType;
        int             fObjectId;
        int             fGeneralId;
        char          * fModule;
        char          * fHelp;

        BBox          * fBox;
        BScrollView   * fScroll;
        BView         * fView;
};

class PreferencesWindow : public BWindow
{
  public:
                            PreferencesWindow( intf_thread_t * p_intf,
                                               BRect frame,
                                               const char * name );
    virtual                 ~PreferencesWindow();

    virtual bool            QuitRequested();
    virtual void            MessageReceived(BMessage* message);
    virtual void            FrameResized( float, float );

            void            Update();
            void            Apply( bool doIt );

            void            ReallyQuit();

  private:
    BView                 * fPrefsView;
    BOutlineListView      * fOutline;
    BView                 * fDummyView;
    ConfigItem            * fCurrent;

    intf_thread_t         * p_intf;
};

#endif    // HAIKU_PREFERENCES_WINDOW_H
