/*****************************************************************************
 * haiku.cpp: Haiku BBitmap video output
 *****************************************************************************
 * Copyright (C) 2000-2010 VLC authors and VideoLAN
 * Copyright (C) 2020-2024 Gerasim Troeglazov (3dEyes**)
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*
 * Like the BeOS video output of VLC 0.x, frames are decoded into BBitmaps
 * and drawn into a plain BWindow; app_server scales them to the window.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_display.h>
#include <vlc_picture_pool.h>
#include <vlc_haiku.h>

#include <Application.h>
#include <Bitmap.h>
#include <Cursor.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#define VOUT_HAIKU_TITLE "VideoLAN Client " VERSION " (BBitmap output)"

static int  Open(vlc_object_t *);
static void Close(vlc_object_t *);

#define FILTER_TEXT N_("Smooth scaling")
#define FILTER_LONGTEXT N_("Filter the picture when it is scaled to the " \
    "window size. Turn this off on slow machines.")

vlc_module_begin ()
    set_shortname("BBitmap")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_description(N_("Haiku BBitmap video output"))
    /* Above OpenGL (270): BBitmap is the default output on Haiku */
    set_capability("vout display", 280)
    add_bool("haiku-vout-filter", true, FILTER_TEXT, FILTER_LONGTEXT, true)
    add_shortcut("haiku", "bbitmap")
    add_obsolete_string("haiku-video-driver")
    set_callbacks(Open, Close)
vlc_module_end ()

/* Messages from the vout thread to the window */
enum
{
    MSG_SET_SIZE       = 'vsiz',
    MSG_SET_FULLSCREEN = 'vfsc',
    MSG_SET_ON_TOP     = 'vtop',
    MSG_HIDE_CURSOR    = 'vhcr',
};

struct picture_sys_t
{
    BBitmap *bitmap;
};

class VideoView;
class VideoWindow;

struct vout_display_sys_t
{
    picture_pool_t *pool;
    VideoWindow    *window;
    VideoView      *view;

    vlc_mutex_t     lock;       /* protects the fields below */
    picture_t      *current;    /* last displayed picture, held */
    BRect           source;     /* visible part of the picture */
    BRect           place;      /* where it goes in the view */
    bool            filter;
};

/*****************************************************************************
 * Key and mouse translation
 *****************************************************************************/
static int ModifiersToVLC(uint32 modifiers)
{
    int code = 0;
    if (modifiers & B_SHIFT_KEY)
        code |= KEY_MODIFIER_SHIFT;
    /* The command key (Alt by default) is what Ctrl is elsewhere */
    if (modifiers & B_COMMAND_KEY)
        code |= KEY_MODIFIER_CTRL;
    if (modifiers & B_CONTROL_KEY)
        code |= KEY_MODIFIER_ALT;
    if (modifiers & B_OPTION_KEY)
        code |= KEY_MODIFIER_META;
    return code;
}

static int KeyToVLC(const BMessage *msg)
{
    int32 key = 0, raw_char = 0;
    uint32 modifiers = 0;
    const char *bytes = NULL;

    msg->FindInt32("key", &key);
    msg->FindInt32("raw_char", &raw_char);
    msg->FindInt32("modifiers", (int32 *)&modifiers);
    msg->FindString("bytes", &bytes);

    int code = KEY_UNSET;
    switch (raw_char)
    {
        case B_ESCAPE:      code = KEY_ESC; break;
        case B_BACKSPACE:   code = KEY_BACKSPACE; break;
        case B_TAB:         code = KEY_TAB; break;
        case B_ENTER:       code = KEY_ENTER; break;
        case B_SPACE:       code = ' '; break;
        case B_LEFT_ARROW:  code = KEY_LEFT; break;
        case B_RIGHT_ARROW: code = KEY_RIGHT; break;
        case B_UP_ARROW:    code = KEY_UP; break;
        case B_DOWN_ARROW:  code = KEY_DOWN; break;
        case B_INSERT:      code = KEY_INSERT; break;
        case B_DELETE:      code = KEY_DELETE; break;
        case B_HOME:        code = KEY_HOME; break;
        case B_END:         code = KEY_END; break;
        case B_PAGE_UP:     code = KEY_PAGEUP; break;
        case B_PAGE_DOWN:   code = KEY_PAGEDOWN; break;
        case B_FUNCTION_KEY:
            if (key >= B_F1_KEY && key <= B_F12_KEY)
                code = KEY_F1 + (key - B_F1_KEY);
            else if (key == B_PAUSE_KEY)
                code = KEY_PAUSE;
            break;
        default:
            if ((modifiers & (B_COMMAND_KEY | B_CONTROL_KEY)) == 0
             && bytes != NULL && (unsigned char)bytes[0] >= 0x20) {
                /* The character typed, with Shift and the keymap applied */
                uint32_t cp;
                const unsigned char *s = (const unsigned char *)bytes;
                if (s[0] < 0x80)
                    cp = s[0];
                else if ((s[0] & 0xe0) == 0xc0)
                    cp = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f);
                else if ((s[0] & 0xf0) == 0xe0)
                    cp = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6)
                       | (s[2] & 0x3f);
                else
                    cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3f) << 12)
                       | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
                return cp;
            }
            if (raw_char >= 0x20 && raw_char < 0x7f)
                code = raw_char;
            break;
    }
    if (code == KEY_UNSET)
        return KEY_UNSET;
    return code | ModifiersToVLC(modifiers);
}

static const struct
{
    uint32 haiku;
    int vlc;
} mouse_buttons[] = {
    { B_PRIMARY_MOUSE_BUTTON,   MOUSE_BUTTON_LEFT },
    { B_SECONDARY_MOUSE_BUTTON, MOUSE_BUTTON_RIGHT },
    { B_TERTIARY_MOUSE_BUTTON,  MOUSE_BUTTON_CENTER },
};

/*****************************************************************************
 * VideoView: draws the current BBitmap, scaled, with black borders
 *****************************************************************************/
class VideoView : public BView
{
public:
    VideoView(BRect frame, vout_display_t *vd)
        : BView(frame, "video", B_FOLLOW_ALL,
                B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
          vd(vd), buttons(0), cursor_hidden(false),
          empty_cursor(B_CURSOR_ID_NO_CURSOR)
    {
        SetViewColor(B_TRANSPARENT_COLOR);
        SetHighColor(0, 0, 0);
    }

    virtual void Draw(BRect)
    {
        vout_display_sys_t *sys = vd->sys;

        vlc_mutex_lock(&sys->lock);
        DrawFrame(sys->current ? sys->current->p_sys->bitmap : NULL,
                  sys->source, sys->place, sys->filter);
        vlc_mutex_unlock(&sys->lock);
    }

    void DrawFrame(BBitmap *bitmap, BRect source, BRect place, bool filter)
    {
        BRect b = Bounds();

        if (bitmap == NULL || !place.IsValid()) {
            FillRect(b);
            return;
        }
        /* Black borders around the picture */
        if (place.top > b.top)
            FillRect(BRect(b.left, b.top, b.right, place.top - 1));
        if (place.bottom < b.bottom)
            FillRect(BRect(b.left, place.bottom + 1, b.right, b.bottom));
        if (place.left > b.left)
            FillRect(BRect(b.left, place.top, place.left - 1, place.bottom));
        if (place.right < b.right)
            FillRect(BRect(place.right + 1, place.top, b.right, place.bottom));

        DrawBitmap(bitmap, source, place,
                   filter ? B_FILTER_BITMAP_BILINEAR : 0);
    }

    virtual void FrameResized(float width, float height)
    {
        vout_display_SendEventDisplaySize(vd, width + 1, height + 1);
    }

    virtual void MouseDown(BPoint where)
    {
        BMessage *msg = Window()->CurrentMessage();
        int32 clicks = 1;
        uint32 now = 0;

        if (msg != NULL) {
            msg->FindInt32("clicks", &clicks);
            msg->FindInt32("buttons", (int32 *)&now);
        }
        ShowCursor();
        SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
        for (size_t i = 0; i < ARRAY_SIZE(mouse_buttons); i++)
            if ((now & mouse_buttons[i].haiku) && !(buttons & mouse_buttons[i].haiku))
                vout_display_SendEventMousePressed(vd, mouse_buttons[i].vlc);
        buttons = now;
        if (clicks == 2 && (now & B_PRIMARY_MOUSE_BUTTON))
            vout_display_SendEventMouseDoubleClick(vd);
        (void) where;
    }

    virtual void MouseUp(BPoint)
    {
        BMessage *msg = Window()->CurrentMessage();
        uint32 now = 0;

        if (msg != NULL)
            msg->FindInt32("buttons", (int32 *)&now);
        for (size_t i = 0; i < ARRAY_SIZE(mouse_buttons); i++)
            if ((buttons & mouse_buttons[i].haiku) && !(now & mouse_buttons[i].haiku))
                vout_display_SendEventMouseReleased(vd, mouse_buttons[i].vlc);
        buttons = now;
    }

    virtual void MouseMoved(BPoint where, uint32 transit, const BMessage *)
    {
        vout_display_sys_t *sys = vd->sys;

        if (transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW)
            return;
        ShowCursor();

        vlc_mutex_lock(&sys->lock);
        vout_display_place_t place;
        place.x = sys->place.left;
        place.y = sys->place.top;
        place.width = sys->place.IntegerWidth() + 1;
        place.height = sys->place.IntegerHeight() + 1;
        vlc_mutex_unlock(&sys->lock);

        vout_display_SendMouseMovedDisplayCoordinates(vd, ORIENT_NORMAL,
                                                      where.x, where.y, &place);
    }

    virtual void KeyDown(const char *, int32)
    {
        BMessage *msg = Window()->CurrentMessage();
        if (msg == NULL)
            return;

        int code = KeyToVLC(msg);
        if (code != KEY_UNSET)
            vout_display_SendEventKey(vd, code);
    }

    void HideCursor()
    {
        if (!cursor_hidden && buttons == 0) {
            SetViewCursor(&empty_cursor);
            cursor_hidden = true;
        }
    }

    void ShowCursor()
    {
        if (cursor_hidden) {
            SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
            cursor_hidden = false;
        }
    }

private:
    vout_display_t *vd;
    uint32 buttons;
    bool cursor_hidden;
    BCursor empty_cursor;
};

/*****************************************************************************
 * VideoWindow
 *****************************************************************************/
class VideoWindow : public BWindow
{
public:
    VideoWindow(BRect frame, const char *title, vout_display_t *vd)
        : BWindow(frame, title, B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
                  B_ASYNCHRONOUS_CONTROLS),
          vd(vd), is_fullscreen(false), is_on_top(false)
    {
        view = new VideoView(Bounds(), vd);
        AddChild(view);
        view->MakeFocus(true);
        SetSizeLimits(32, 30000, 32, 30000);
    }

    VideoView *View() const { return view; }

    virtual void MessageReceived(BMessage *msg)
    {
        switch (msg->what)
        {
            case MSG_SET_SIZE:
            {
                int32 width, height;
                if (!is_fullscreen
                 && msg->FindInt32("width", &width) == B_OK
                 && msg->FindInt32("height", &height) == B_OK) {
                    BRect b = view->Bounds();
                    if (b.IntegerWidth() + 1 != width
                     || b.IntegerHeight() + 1 != height)
                        ResizeTo(width - 1, height - 1);
                }
                break;
            }
            case MSG_SET_FULLSCREEN:
                SetFullscreen(msg->FindBool("fullscreen"));
                break;
            case MSG_SET_ON_TOP:
                is_on_top = msg->FindBool("on-top");
                if (!is_fullscreen)
                    SetFeel(is_on_top ? B_FLOATING_ALL_WINDOW_FEEL
                                      : B_NORMAL_WINDOW_FEEL);
                break;
            case MSG_HIDE_CURSOR:
                view->HideCursor();
                break;
            case B_SIMPLE_DATA:
                /* Files dropped on the video: let the interface play them */
                if (be_app != NULL)
                    be_app->PostMessage(msg);
                break;
            default:
                BWindow::MessageReceived(msg);
                break;
        }
    }

    virtual void Zoom(BPoint, float, float)
    {
        vout_display_SendEventFullscreen(vd, !is_fullscreen, false);
    }

    virtual bool QuitRequested()
    {
        /* Closing the video window stops playback; the window goes away
         * with the video output. */
        vout_display_SendEventKey(vd, KEY_MEDIA_STOP);
        return false;
    }

    void SetFullscreen(bool fullscreen)
    {
        if (fullscreen == is_fullscreen)
            return;
        if (fullscreen) {
            BScreen screen(this);
            BRect frame = screen.Frame();

            saved_frame = Frame();
            SetLook(B_NO_BORDER_WINDOW_LOOK);
            SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
            SetFlags(Flags() | B_NOT_RESIZABLE | B_NOT_MOVABLE);
            MoveTo(frame.LeftTop());
            ResizeTo(frame.Width(), frame.Height());
            Activate();
        } else {
            SetLook(B_TITLED_WINDOW_LOOK);
            SetFeel(is_on_top ? B_FLOATING_ALL_WINDOW_FEEL
                              : B_NORMAL_WINDOW_FEEL);
            SetFlags(Flags() & ~(B_NOT_RESIZABLE | B_NOT_MOVABLE));
            MoveTo(saved_frame.LeftTop());
            ResizeTo(saved_frame.Width(), saved_frame.Height());
            view->ShowCursor();
        }
        is_fullscreen = fullscreen;
    }

private:
    vout_display_t *vd;
    VideoView *view;
    BRect saved_frame;
    bool is_fullscreen;
    bool is_on_top;
};

/*****************************************************************************
 * Helpers
 *****************************************************************************/
/* Recomputes where the picture goes; call with sys->lock held */
static void UpdatePlace(vout_display_t *vd, const vout_display_cfg_t *cfg)
{
    vout_display_sys_t *sys = vd->sys;
    vout_display_place_t place;

    vout_display_PlacePicture(&place, &vd->source, cfg, false);
    sys->place = BRect(place.x, place.y,
                       place.x + place.width - 1, place.y + place.height - 1);

    if (vd->source.orientation == ORIENT_NORMAL)
        sys->source = BRect(vd->source.i_x_offset, vd->source.i_y_offset,
                            vd->source.i_x_offset + vd->source.i_visible_width - 1,
                            vd->source.i_y_offset + vd->source.i_visible_height - 1);
    else
        sys->source = BRect(vd->fmt.i_x_offset, vd->fmt.i_y_offset,
                            vd->fmt.i_x_offset + vd->fmt.i_visible_width - 1,
                            vd->fmt.i_y_offset + vd->fmt.i_visible_height - 1);
}

static void Redraw(vout_display_t *vd)
{
    vout_display_sys_t *sys = vd->sys;

    if (sys->view->LockLooperWithTimeout(100000) == B_OK) {
        sys->view->Invalidate();
        sys->view->UnlockLooper();
    }
}

static void PostSize(vout_display_t *vd, unsigned width, unsigned height)
{
    BMessage msg(MSG_SET_SIZE);
    msg.AddInt32("width", width);
    msg.AddInt32("height", height);
    vd->sys->window->PostMessage(&msg);
}

/* Fits a default display size on the screen, keeping its aspect */
static void FitOnScreen(unsigned *width, unsigned *height)
{
    BScreen screen(B_MAIN_SCREEN_ID);
    BRect frame = screen.Frame();
    float max_w = (frame.Width() + 1) * 0.9f, max_h = (frame.Height() + 1) * 0.85f;
    float w = *width, h = *height;

    if (w > max_w) {
        h = h * max_w / w;
        w = max_w;
    }
    if (h > max_h) {
        w = w * max_h / h;
        h = max_h;
    }
    *width = w > 32 ? w : 32;
    *height = h > 32 ? h : 32;
}

/*****************************************************************************
 * Pictures
 *****************************************************************************/
static void DestroyPicture(picture_t *picture)
{
    delete picture->p_sys->bitmap;
    free(picture->p_sys);
    free(picture);
}

static picture_pool_t *Pool(vout_display_t *vd, unsigned count)
{
    vout_display_sys_t *sys = vd->sys;

    if (sys->pool != NULL)
        return sys->pool;

    /* One more than asked for, since the display keeps the last picture
     * to redraw the window */
    count++;
    picture_t *pictures[count];
    unsigned i;

    for (i = 0; i < count; i++) {
        BBitmap *bitmap = new BBitmap(BRect(0, 0, vd->fmt.i_width - 1,
                                            vd->fmt.i_height - 1), B_RGB32);
        picture_sys_t *psys = (picture_sys_t *)malloc(sizeof(*psys));
        if (bitmap->InitCheck() != B_OK || psys == NULL) {
            delete bitmap;
            free(psys);
            break;
        }
        memset(bitmap->Bits(), 0, bitmap->BitsLength());
        psys->bitmap = bitmap;

        picture_resource_t rsc;
        memset(&rsc, 0, sizeof(rsc));
        rsc.p_sys = psys;
        rsc.pf_destroy = DestroyPicture;
        rsc.p[0].p_pixels = (uint8_t *)bitmap->Bits();
        rsc.p[0].i_pitch = bitmap->BytesPerRow();
        rsc.p[0].i_lines = vd->fmt.i_height;

        pictures[i] = picture_NewFromResource(&vd->fmt, &rsc);
        if (pictures[i] == NULL) {
            delete bitmap;
            free(psys);
            break;
        }
    }
    if (i == 0)
        return NULL;

    sys->pool = picture_pool_New(i, pictures);
    if (sys->pool == NULL)
        while (i > 0)
            picture_Release(pictures[--i]);
    return sys->pool;
}

static void Display(vout_display_t *vd, picture_t *picture,
                    subpicture_t *subpicture)
{
    vout_display_sys_t *sys = vd->sys;

    vlc_mutex_lock(&sys->lock);
    picture_t *previous = sys->current;
    sys->current = picture;
    BRect source = sys->source, place = sys->place;
    bool filter = sys->filter;
    vlc_mutex_unlock(&sys->lock);

    if (sys->view->LockLooperWithTimeout(100000) == B_OK) {
#ifdef __HAIKU__
        /* Draw now and wait for app_server, so that frames don't pile up */
        sys->view->DrawFrame(picture->p_sys->bitmap, source, place, filter);
        sys->view->Sync();
#else
        /* Cosmoe and VitruvianOS only show what Draw() paints: ask for a
         * redraw of the picture, which stays held until the next one */
        sys->view->Invalidate();
        VLC_UNUSED(source); VLC_UNUSED(place); VLC_UNUSED(filter);
#endif
        sys->view->UnlockLooper();
    }

    if (previous != NULL)
        picture_Release(previous);
    VLC_UNUSED(subpicture);
}

/*****************************************************************************
 * Control
 *****************************************************************************/
static int Control(vout_display_t *vd, int query, va_list args)
{
    vout_display_sys_t *sys = vd->sys;

    switch (query)
    {
        case VOUT_DISPLAY_HIDE_MOUSE:
            sys->window->PostMessage(MSG_HIDE_CURSOR);
            return VLC_SUCCESS;

        case VOUT_DISPLAY_CHANGE_FULLSCREEN:
        {
            BMessage msg(MSG_SET_FULLSCREEN);
            msg.AddBool("fullscreen", va_arg(args, int) != 0);
            sys->window->PostMessage(&msg);
            return VLC_SUCCESS;
        }

        case VOUT_DISPLAY_CHANGE_WINDOW_STATE:
        {
            unsigned state = va_arg(args, unsigned);
            BMessage msg(MSG_SET_ON_TOP);
            msg.AddBool("on-top", (state & VOUT_WINDOW_STATE_ABOVE) != 0);
            sys->window->PostMessage(&msg);
            return VLC_SUCCESS;
        }

        case VOUT_DISPLAY_CHANGE_DISPLAY_SIZE:
        {
            const vout_display_cfg_t *cfg = va_arg(args, const vout_display_cfg_t *);
            vlc_mutex_lock(&sys->lock);
            UpdatePlace(vd, cfg);
            vlc_mutex_unlock(&sys->lock);
            if (!cfg->is_fullscreen)
                PostSize(vd, cfg->display.width, cfg->display.height);
            Redraw(vd);
            return VLC_SUCCESS;
        }

        case VOUT_DISPLAY_CHANGE_ZOOM:
        {
            const vout_display_cfg_t *cfg = va_arg(args, const vout_display_cfg_t *);
            if (!cfg->is_fullscreen) {
                unsigned width, height;
                vout_display_GetDefaultDisplaySize(&width, &height,
                                                   &vd->source, cfg);
                FitOnScreen(&width, &height);
                PostSize(vd, width, height);
            }
            vlc_mutex_lock(&sys->lock);
            UpdatePlace(vd, cfg);
            vlc_mutex_unlock(&sys->lock);
            Redraw(vd);
            return VLC_SUCCESS;
        }

        case VOUT_DISPLAY_CHANGE_DISPLAY_FILLED:
        {
            const vout_display_cfg_t *cfg = va_arg(args, const vout_display_cfg_t *);
            vlc_mutex_lock(&sys->lock);
            UpdatePlace(vd, cfg);
            vlc_mutex_unlock(&sys->lock);
            Redraw(vd);
            return VLC_SUCCESS;
        }

        case VOUT_DISPLAY_CHANGE_SOURCE_ASPECT:
        case VOUT_DISPLAY_CHANGE_SOURCE_CROP:
            vlc_mutex_lock(&sys->lock);
            UpdatePlace(vd, vd->cfg);
            vlc_mutex_unlock(&sys->lock);
            Redraw(vd);
            return VLC_SUCCESS;

        case VOUT_DISPLAY_RESET_PICTURES:
        case VOUT_DISPLAY_CHANGE_VIEWPOINT:
        default:
            return VLC_EGENERIC;
    }
}

/*****************************************************************************
 * Open / Close
 *****************************************************************************/
static int Open(vlc_object_t *object)
{
    vout_display_t *vd = (vout_display_t *)object;

    if (vlc_haiku_app_Hold(vd))
        return VLC_EGENERIC;

    vout_display_sys_t *sys = (vout_display_sys_t *)calloc(1, sizeof(*sys));
    if (sys == NULL) {
        vlc_haiku_app_Release();
        return VLC_ENOMEM;
    }
    vd->sys = sys;
    vlc_mutex_init(&sys->lock);
    sys->filter = var_InheritBool(vd, "haiku-vout-filter");

    /* Pictures are BBitmaps of the source size, in the B_RGB32 layout */
    video_format_t fmt;
    video_format_ApplyRotation(&fmt, &vd->fmt);
    fmt.i_chroma = VLC_CODEC_RGB32;
    fmt.i_rmask = 0x00ff0000;
    fmt.i_gmask = 0x0000ff00;
    fmt.i_bmask = 0x000000ff;
    vd->fmt = fmt;

    unsigned width, height;
    vout_display_GetDefaultDisplaySize(&width, &height, &vd->source, vd->cfg);
    FitOnScreen(&width, &height);

    BScreen screen(B_MAIN_SCREEN_ID);
    BRect sf = screen.Frame();
    float left = sf.left + ((sf.Width() + 1) - width) / 2;
    float top = sf.top + ((sf.Height() + 1) - height) / 2;
    BRect frame(left, top, left + width - 1, top + height - 1);

    const char *title = vd->cfg->display.title;
    sys->window = new VideoWindow(frame, title ? title : VOUT_HAIKU_TITLE, vd);
    sys->view = sys->window->View();

    vout_display_cfg_t cfg = *vd->cfg;
    cfg.display.width = width;
    cfg.display.height = height;
    UpdatePlace(vd, &cfg);

    vd->info.has_double_click = true;
    vd->info.needs_hide_mouse = true;
    vd->pool    = Pool;
    vd->prepare = NULL;
    vd->display = Display;
    vd->control = Control;
    vd->manage  = NULL;

    sys->window->Show();
    vout_display_SendEventDisplaySize(vd, width, height);
    if (vd->cfg->is_fullscreen)
        vout_display_SendEventFullscreen(vd, true, false);
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *object)
{
    vout_display_t *vd = (vout_display_t *)object;
    vout_display_sys_t *sys = vd->sys;

    if (sys->window->Lock())
        sys->window->Quit();

    if (sys->current != NULL)
        picture_Release(sys->current);
    if (sys->pool != NULL)
        picture_pool_Release(sys->pool);
    vlc_mutex_destroy(&sys->lock);
    free(sys);
    vlc_haiku_app_Release();
}
