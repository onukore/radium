/* Copyright 2000 Kjetil S. Matheussen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */

#include "nsmtracker.h"
#include "common_proc.h"
#include "tracks_proc.h"
#include "gfx_wtracks_proc.h"
#include "trackreallines_proc.h"
#include "undo_notes_proc.h"
#include "gfx_statusbar_proc.h"
#include "player_proc.h"
#include "placement_proc.h"

#include "mouse_fxarea_proc.h"

static int MoveNoteName(
	struct Tracker_Windows *window,
	int x,int y
){
  struct WBlocks *wblock=window->wblock;
  struct MouseAction *action= &window->prevaction;

  struct WTracks *wtrack=(struct WTracks *)action->pointer1;
  //struct Tracks *track=wtrack->track;
  struct Notes *note=(struct Notes *)action->pointer2;

  Place place;
  GetReallineAndPlaceFromY(window, wblock, y, &place, NULL, NULL);

  int org_x = action->eint1;
  //int org_y = action->eint2;
  float org_pitch = action->efloat1;

  float pitch=org_pitch + (x-org_x)/20; // 20 pixels per note.
  note->note = R_BOUNDARIES(1,pitch,127);

  PlaceCopy(&note->l.p, &place);

  UpdateTrackReallines(window,wblock,wtrack);
  DrawUpWTrack(window,wblock,wtrack);

  return 0;
}

static struct Notes *GetNoteNameUnderMouse(struct Tracker_Windows *window, struct WTracks *wtrack, int realline, int x, int y){
  struct TrackRealline *trackrealline= &wtrack->trackreallines[realline];
  struct TrackReallineElements *element;

  for(element=trackrealline->trackreallineelements;element!=NULL;element=element->next){
    switch(element->type){
    case TRE_THISNOTELINES:
      return element->pointer;
    }
  }

  return NULL;
}

bool SetMouseActionPitches(
	struct Tracker_Windows *window,
	struct MouseAction *action,
	struct WTracks *wtrack,
	int x,int y,
	int click
){
	struct WBlocks *wblock=window->wblock;
        Place place;

        if (click != 1)
          return false;

	int realline = GetReallineAndPlaceFromY(window,wblock,y,&place,NULL,NULL);
	if(realline<0)
          return false;

        struct Notes *note = GetNoteNameUnderMouse(window, wtrack, realline, x, y);
        if (note!= NULL) {
          action->action = PITCH_NOTENAME;
          action->pointer1 = wtrack;
          action->pointer2 = note;

          action->eint1 = x;
          action->eint2 = y;
          action->efloat1 = note->note;

          action->MouseUpFunction = &MoveNoteName;

          PlayStop();
          Undo_Notes(window,window->wblock->block,wtrack->track,window->wblock->curr_realline);

          return true;
        }

        return false;
}





