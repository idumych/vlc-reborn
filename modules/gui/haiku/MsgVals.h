/*****************************************************************************
 * MsgVals.h
 *****************************************************************************
 * Copyright (C) 2001 the VideoLAN team
 * $Id: 40591be833ab4f6620b4d6e79456682f8fc38ff8 $
 *
 * Authors: Tony Castley <tcastley@mail.powerup.com.au>
 *          Stephan Aßmus <stippi@yellowbites.com>
 *
 * This program is free software you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef HAIKU_MESSAGE_VALUES_H
#define HAIKU_MESSAGE_VALUES_H

#define PLAYING       0
#define PAUSED        1

#define OPEN_FILE          'opfl'
#define OPEN_DVD           'opdv'
#define LOAD_SUBFILE       'losu'
#define SUBFILE_RECEIVED   'sure'
#define OPEN_PLAYLIST      'oppl'
#define STOP_PLAYBACK      'stpl'
#define START_PLAYBACK     'play'
#define PAUSE_PLAYBACK     'papl'
#define HEIGHTH_PLAY       'hhpl'
#define QUARTER_PLAY       'qupl'
#define HALF_PLAY          'hapl'
#define NORMAL_PLAY        'nrpl'
#define TWICE_PLAY         'twpl'
#define FOUR_PLAY          'fopl'
#define HEIGHT_PLAY        'hepl'
#define SEEK_PLAYBACK      'seek'
#define VOLUME_CHG         'voch'
#define VOLUME_MUTE        'mute'
#define SELECT_CHANNEL     'chan'
#define SELECT_SUBTITLE    'subt'
#define PREV_TITLE         'prti'
#define NEXT_TITLE         'nxti'
#define TOGGLE_TITLE       'tgti'
#define PREV_CHAPTER       'prch'
#define NEXT_CHAPTER       'nxch'
#define TOGGLE_CHAPTER     'tgch'
#define PREV_FILE          'prfl'
#define NEXT_FILE          'nxfl'
#define NAVIGATE_PREV      'navp'    // could be chapter, title or file
#define NAVIGATE_NEXT      'navn'    // could be chapter, title or file
#define OPEN_PREFERENCES   'pref'
#define OPEN_MESSAGES      'mess'
#define TOGGLE_ON_TOP      'ontp'
#define SHOW_INTERFACE     'shin'
#define TOGGLE_FULL_SCREEN 'tgfs'
#define RESIZE_50          'rshl'
#define RESIZE_100         'rsor'
#define RESIZE_200         'rsdb'
#define RESIZE_TRUE        'rstr'
#define ASPECT_CORRECT     'asco'
#define VERT_SYNC          'vsyn'
#define WINDOW_FEEL        'wfel'
#define SCREEN_SHOT        'scrn'
#define MSG_UPDATE         'updt'
#define MSG_SOUNDPLAY      'move'  // drag'n'drop from soundplay playlist
#define SHORTCUT           'shcu'
#define VOLUME_UP          'vlup'
#define VOLUME_DOWN        'vldn'
#define OPEN_LOCATION      'oplo'
#define LOCATION_ENTERED   'loen'
#define PLAY_ITEM          'plit'
#define REMOVE_ITEMS       'rmit'
#define CLEAR_PLAYLIST     'clpl'
#define PLAYLIST_CHANGED   'plch'
#define LOG_MESSAGE        'logm'

#endif    // HAIKU_MESSAGE_VALUES_H
