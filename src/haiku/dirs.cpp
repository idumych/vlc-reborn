/*****************************************************************************
 * dirs.c: XDG directories configuration
 *****************************************************************************
 * Copyright (C) 2001-2007 VLC authors and VideoLAN
 * Copyright © 2007-2009 Rémi Denis-Courmont
 *
 * Authors: Gildas Bazin <gbazin@videolan.org>
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

#include "../libvlc.h"
#include "config/configuration.h"

#include <unistd.h>
#include <libgen.h>

#include <FindDirectory.h>

char *config_GetDataDir (void)
{
    return strdup (PKGDATADIR);
}

char *config_GetLibDir (void)
{
    return strdup (PKGLIBDIR);
}

static char *config_GetHaikuDir (directory_which which, char *leaf = NULL)
{
	char path[B_PATH_NAME_LENGTH];
	find_directory(which, 0, false, path, B_PATH_NAME_LENGTH);
	if (leaf != NULL) {
		strcat(path, "/");
		strcat(path, leaf);
	}
	return strdup (path);
}

char *config_GetUserDir (vlc_userdir_t type)
{
    switch (type)
    {
        case VLC_HOME_DIR:
            break;
        case VLC_CONFIG_DIR:
        case VLC_DATA_DIR:
            return config_GetHaikuDir (B_USER_SETTINGS_DIRECTORY, "vlc");
        case VLC_CACHE_DIR:
			return config_GetHaikuDir (B_USER_CACHE_DIRECTORY, "vlc");

        case VLC_DESKTOP_DIR:
        	return config_GetHaikuDir (B_DESKTOP_DIRECTORY);
        case VLC_DOWNLOAD_DIR:
        case VLC_TEMPLATES_DIR:
        case VLC_PUBLICSHARE_DIR:
        case VLC_DOCUMENTS_DIR:
        case VLC_MUSIC_DIR:
        case VLC_PICTURES_DIR:
        case VLC_VIDEOS_DIR:
			break;
    }
    return config_GetHaikuDir (B_USER_DIRECTORY);
}
