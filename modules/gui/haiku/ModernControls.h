/*****************************************************************************
 * ModernControls.h: controls of the modern (VLC 3) Haiku interface
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

#ifndef HAIKU_MODERN_CONTROLS_H
#define HAIKU_MODERN_CONTROLS_H

#include <Control.h>
#include <Messenger.h>
#include <StringView.h>
#include <View.h>

/* Icons drawn with the Interface Kit, in the panel text colour */
enum
{
    GLYPH_PLAY,
    GLYPH_PAUSE,
    GLYPH_STOP,
    GLYPH_PREV,
    GLYPH_NEXT,
    GLYPH_FULLSCREEN,
    GLYPH_LEAVE_FULLSCREEN,
    GLYPH_PLAYLIST,
    GLYPH_LOOP,
    GLYPH_LOOP_ONE,
    GLYPH_RANDOM,
    GLYPH_VOLUME,
    GLYPH_MUTED,
};

void DrawGlyph( BView * view, BRect frame, int glyph, rgb_color color );

/* The VLC cone, fitted in frame */
void DrawCone( BView * view, BRect frame );

/* Flat button with an icon, framed when the mouse is over it; a toggle
 * button stays framed while its value is B_CONTROL_ON */
class GlyphButton : public BControl
{
 public:
                            GlyphButton( const char * name, int glyph,
                                         BMessage * message,
                                         float scale = 1.0f );

            void            SetGlyph( int glyph );
            int             Glyph() const { return fGlyph; }

    virtual void            AttachedToWindow();
    virtual void            Draw( BRect updateRect );
    virtual void            MouseDown( BPoint where );
    virtual void            MouseUp( BPoint where );
    virtual void            MouseMoved( BPoint where, uint32 transit,
                                        const BMessage * dragMessage );
    virtual BSize           MinSize();
    virtual BSize           MaxSize();
    virtual BSize           PreferredSize();
    virtual void            GetPreferredSize( float * width, float * height );

 private:
    int                     fGlyph;
    float                   fScale;
    bool                    fPressed;
    bool                    fInside;
};

/* Seek bar: value 0 to 10000; sends its message with "position" (float,
 * 0 to 1) while dragged */
class ModernSeekSlider : public BControl
{
 public:
                            ModernSeekSlider( const char * name, BMessage * message );

            void            SetPosition( float position );
            bool            IsTracking() const { return fTracking; }

    virtual void            AttachedToWindow();
    virtual void            Draw( BRect updateRect );
    virtual void            MouseDown( BPoint where );
    virtual void            MouseUp( BPoint where );
    virtual void            MouseMoved( BPoint where, uint32 transit,
                                        const BMessage * dragMessage );
    virtual BSize           MinSize();
    virtual BSize           MaxSize();
    virtual BSize           PreferredSize();

 private:
            BRect           _BarFrame() const;
            float           _PositionFor( BPoint where ) const;
            void            _Send( float position );

    bool                    fTracking;
    bigtime_t               fLastSent;
};

/* Volume wedge, 0 to 125 %; sends its message with "volume" (int32, %) */
class ModernVolumeSlider : public BControl
{
 public:
                            ModernVolumeSlider( const char * name, BMessage * message );

            void            SetVolume( int32 percent );

    virtual void            AttachedToWindow();
    virtual void            Draw( BRect updateRect );
    virtual void            MouseDown( BPoint where );
    virtual void            MouseUp( BPoint where );
    virtual void            MouseMoved( BPoint where, uint32 transit,
                                        const BMessage * dragMessage );
    virtual void            MessageReceived( BMessage * message );
    virtual BSize           MinSize();
    virtual BSize           MaxSize();
    virtual BSize           PreferredSize();

 private:
            void            _SetFromPoint( BPoint where );
            void            _Send();

    bool                    fTracking;
};

/* Time next to the seek bar; a click sends TOGGLE_REMAINING */
class TimeLabel : public BStringView
{
 public:
                            TimeLabel( const char * name );
            void            SetTarget( BMessenger target ) { fTarget = target; }
    virtual void            MouseDown( BPoint where );
    virtual BSize           MinSize();

 private:
    BMessenger              fTarget;
};

/* What the control bar shows */
struct ControlState
{
    bool        has_input;
    bool        can_play;       /* playlist not empty */
    bool        playing;
    bool        can_seek;
    bool        muted;
    bool        fullscreen;
    bool        playlist_shown;
    bool        random;
    bool        remaining;
    int         loop;           /* 0 off, 1 all, 2 one */
    float       position;
    int64_t     time;           /* microseconds */
    int64_t     length;
    int32       volume;         /* percent */
};

/* The seek bar row and the buttons row of VLC 3 */
class ControlBar : public BView
{
 public:
                            ControlBar( const char * name, bool fullscreen );

            void            SetTarget( BMessenger target );
            void            Update( const ControlState & state );

 private:
    TimeLabel             * fElapsed;
    ModernSeekSlider            * fSeek;
    TimeLabel             * fTotal;
    GlyphButton           * fPlay;
    GlyphButton           * fPrev;
    GlyphButton           * fStop;
    GlyphButton           * fNext;
    GlyphButton           * fFullscreen;
    GlyphButton           * fPlaylist;
    GlyphButton           * fLoop;
    GlyphButton           * fRandom;
    GlyphButton           * fMute;
    ModernVolumeSlider          * fVolume;
    ControlState            fState;
    bool                    fFirst;
};

/* Formats a time like VLC 3 does ("1:02:03", "02:03") */
void format_time( char * buffer, size_t size, int64_t time );

#endif    // HAIKU_MODERN_CONTROLS_H
