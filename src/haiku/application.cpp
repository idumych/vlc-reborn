/*****************************************************************************
 * application.cpp: Haiku BApplication management
 *****************************************************************************
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_haiku.h>

#include <Application.h>
#include <Messenger.h>
#include <OS.h>

#include <assert.h>

#define VLC_APP_SIGNATURE "application/x-vnd.vlc"

/* The BApplication libvlccore runs when the host program has none.
 * It lives in libvlccore rather than in a plugin, so that its thread never
 * runs code from a plugin that has been unloaded. */
class VLCApplication : public BApplication
{
public:
    VLCApplication(libvlc_int_t *libvlc)
        : BApplication(VLC_APP_SIGNATURE), libvlc(libvlc), quitting(false)
    {
    }

    virtual void RefsReceived(BMessage *msg)
    {
        Forward(msg);
    }

    virtual void AboutRequested()
    {
        BMessage msg(B_ABOUT_REQUESTED);
        Forward(&msg);
    }

    virtual void MessageReceived(BMessage *msg)
    {
        if (msg->what == B_SIMPLE_DATA)
            Forward(msg);
        else
            BApplication::MessageReceived(msg);
    }

    virtual bool QuitRequested()
    {
        if (quitting)
            return BApplication::QuitRequested();
        /* Quitting from the Deskbar (or Alt+Q on a window without a menu
         * for it) stops LibVLC; the application itself goes away when the
         * last plugin lets go of it. */
        libvlc_Quit(libvlc);
        return false;
    }

    void SetTarget(BLooper *looper)
    {
        if (Lock()) {
            target = looper ? BMessenger(looper) : BMessenger();
            Unlock();
        }
    }

    void Shutdown()
    {
        if (Lock()) {
            quitting = true;
            Unlock();
        }
        PostMessage(B_QUIT_REQUESTED);
    }

private:
    void Forward(BMessage *msg)
    {
        if (target.IsValid())
            target.SendMessage(msg);
    }

    libvlc_int_t *libvlc;
    BMessenger target;
    bool quitting;
};

static vlc_mutex_t lock = VLC_STATIC_MUTEX;
static unsigned refs = 0;
static thread_id app_thread = -1;
static VLCApplication *app = NULL;
static void *pending_target = NULL;

struct app_start
{
    libvlc_int_t *libvlc;
    sem_id ready;
};

static int32 AppThread(void *data)
{
    struct app_start *start = (struct app_start *)data;

    app = new VLCApplication(start->libvlc);
    if (pending_target != NULL)
        app->SetTarget((BLooper *)pending_target);
    release_sem(start->ready);

    app->Run();
    delete app;
    app = NULL;
    return 0;
}

#undef vlc_haiku_app_Hold
int vlc_haiku_app_Hold(vlc_object_t *obj)
{
    int ret = VLC_SUCCESS;

    vlc_mutex_lock(&lock);
    if (refs++ == 0 && be_app == NULL) {
        struct app_start start = { obj->obj.libvlc, create_sem(0, "vlc app") };

        app_thread = spawn_thread(AppThread, "vlc application",
                                  B_DISPLAY_PRIORITY, &start);
        if (start.ready < B_OK || app_thread < B_OK
         || resume_thread(app_thread) != B_OK) {
            msg_Err(obj, "cannot start the application thread");
            if (app_thread >= B_OK)
                kill_thread(app_thread);
            app_thread = -1;
            refs--;
            ret = VLC_EGENERIC;
        } else {
            acquire_sem(start.ready);
            msg_Dbg(obj, "started the application (%s)", VLC_APP_SIGNATURE);
        }
        if (start.ready >= B_OK)
            delete_sem(start.ready);
    }
    vlc_mutex_unlock(&lock);
    return ret;
}

void vlc_haiku_app_Release(void)
{
    vlc_mutex_lock(&lock);
    assert(refs > 0);
    if (--refs == 0 && app_thread >= B_OK) {
        status_t exit_value;

        app->Shutdown();
        wait_for_thread(app_thread, &exit_value);
        app_thread = -1;
    }
    vlc_mutex_unlock(&lock);
}

void vlc_haiku_app_SetTarget(void *looper)
{
    vlc_mutex_lock(&lock);
    pending_target = looper;
    if (app != NULL)
        app->SetTarget((BLooper *)looper);
    vlc_mutex_unlock(&lock);
}
