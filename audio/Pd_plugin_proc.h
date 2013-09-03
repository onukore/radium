/* Copyright 2013 Kjetil S. Matheussen

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


#ifndef AUDIO_PD_PLUGIN_PROC_H
#define AUDIO_PD_PLUGIN_PROC_H

#include "Pd_plugin.h"

extern LANGSPEC void PD_set_controller_name(SoundPlugin *plugin, int n, const char *name);
extern LANGSPEC Pd_Controller *PD_get_controller(SoundPlugin *plugin, int n);
extern LANGSPEC void PD_set_qtgui(SoundPlugin *plugin, void *qtgui);
extern LANGSPEC void PD_delete_controller(SoundPlugin *plugin, int controller_num);

extern LANGSPEC void PD_recreate_controllers_from_state(SoundPlugin *plugin, hash_t *state);
extern LANGSPEC void PD_create_controllers_from_state(SoundPlugin *plugin, hash_t *state);

#endif // AUDIO_PD_PLUGIN_PROC_H