/* Copyright 2012 Kjetil S. Matheussen

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


#include "../common/patch_proc.h"
#include "../common/undo_patchname_proc.h"

#include "Qt_patch_widget.h"

#ifndef USE_QT5
#include <QCleanlooksStyle>
#endif

class Patch_widget : public QWidget, public GL_PauseCaller, public Ui::Patch_widget{
  Q_OBJECT;

 public:
  bool initing;

  struct Patch *_patch;
  struct PatchVoice *_voices;

#ifndef USE_QT5
  QCleanlooksStyle _cleanlooksStyle;
#endif
  
  Patch_widget(QWidget *parent, struct Patch *patch)
    : QWidget(parent)
    , _patch(patch)
    , _voices(&patch->voices[0])
  {    
    initing = true;
    /*
    GL_lock(); {
      GL_pause_gl_thread_a_short_while(); // might fix [1] crash
    }GL_unlock();
    */
    setupUi(this);

    for(int i=0;i<NUM_PATCH_VOICES;i++){
      
#ifndef USE_QT5
      get_t(i)->setStyle(&_cleanlooksStyle);
      get_v(i)->setStyle(&_cleanlooksStyle);
      get_s(i)->setStyle(&_cleanlooksStyle);
      get_l(i)->setStyle(&_cleanlooksStyle);
      get_c(i)->setStyle(&_cleanlooksStyle);
#endif

      set_fixed_widget_width(get_o(i), "xx");    // onoff
      set_fixed_widget_width(get_t(i), "-24.00 "); // transpose
      set_fixed_widget_width(get_v(i), "-35 "); // volume
      set_fixed_widget_width(get_s(i), "999.9 "); // start
      set_fixed_widget_width(get_l(i), "999.9 "); //length
      set_fixed_widget_width(get_c(i), "256 "); // chance
      
      get_o(i)->setToolTip("Whether to play this voice. At least one voice must be selected in order for any notes to be played.");
      get_t(i)->setToolTip("How much to transpose this voice");
      get_v(i)->setToolTip("Volume (in dB) for this voice. MIDI and FluidSynth does not support higher values than 0 dB.");
      get_s(i)->setToolTip("How long time until this voice starts playing\n"
                           "The unit is milliseconds.");//Beats. To use milliseconds instead, press the \"ms.\" button.");
      get_l(i)->setToolTip("A value higher than 0.0 will override the duration of this voice.\n"
                           "The unit is milliseconds.");//by default Beats, unless \"ms.\" is selected");

      get_f(i)->hide();
      get_c(i)->setToolTip("The chance of this voice to play. A value of 256 means that there is 100% chance of this voice plaing.\n"
                           "A value of 128 means that there is a 50% chance of this voice playing.");
      timeformat_label->hide();
    }
    
    updateWidgets();
    initing = false;
  }

  void set_fixed_widget_width(QWidget *widget, QString text){
    QFontMetrics fm(font());
    int width = fm.width(text);
    widget->setFixedWidth(width);
    widget->setMinimumWidth(width);
    widget->setMaximumWidth(width);
  }
  
  MyQCheckBox *get_o(int i){
    MyQCheckBox *o[NUM_PATCH_VOICES]={o1,o2,o3,o4,o5,o6,o7};
    return o[i];
  }

#if 0
  MyQSpinBox *get_t(int i){
    MyQSpinBox *t[NUM_PATCH_VOICES]={t1,t2,t3,t4,t5,t6,t7};
    return t[i];
  }
#else
  FocusSnifferQDoubleSpinBox *get_t(int i){
    FocusSnifferQDoubleSpinBox *t[NUM_PATCH_VOICES]={t1,t2,t3,t4,t5,t6,t7};
    return t[i];
  }
#endif
  
  MyQSpinBox *get_v(int i){
    MyQSpinBox *v[NUM_PATCH_VOICES]={v1,v2,v3,v4,v5,v6,v7};
    return v[i];
  }

  FocusSnifferQDoubleSpinBox *get_s(int i){
    FocusSnifferQDoubleSpinBox *s[NUM_PATCH_VOICES]={s1,s2,s3,s4,s5,s6,s7};
    return s[i];
  }

  FocusSnifferQDoubleSpinBox *get_l(int i){
    FocusSnifferQDoubleSpinBox *l[NUM_PATCH_VOICES]={l1,l2,l3,l4,l5,l6,l7};
    return l[i];
  }

  MyQCheckBox *get_f(int i){
    MyQCheckBox *f[NUM_PATCH_VOICES]={f1,f2,f3,f4,f5,f6,f7};
    return f[i];
  }

  MyQSpinBox *get_c(int i){
    MyQSpinBox *c[NUM_PATCH_VOICES]={c1,c2,c3,c4,c5,c6,c7};
    return c[i];
  }

  void update_label_color(QLabel *l){
    QString text = l->text();
    int pos = text.indexOf("#");
    l->setText(text.replace(pos, 7, get_qcolor(TEXT_COLOR_NUM).name()));
  }
  
  void updateWidgets(){

    for(int i=0;i<NUM_PATCH_VOICES;i++){
      PatchVoice *voice=&_voices[i];

      get_o(i)->setChecked(voice->is_on);
      get_o(i)->_patch = _patch;
      get_o(i)->_effect_num = i;
      get_o(i)->_undo_patchvoice = true;

      get_t(i)->setValue(voice->transpose);
      get_v(i)->setValue(voice->volume);
      get_s(i)->setValue(voice->start);
      get_l(i)->setValue(voice->length);
      get_c(i)->setValue(voice->chance);
    }

    // bad 1

    name_widget->setText(_patch->name);
    through_onoff->setChecked(_patch->forward_events);

    update_label_color(nd_label1);
    update_label_color(nd_label2);
    update_label_color(nd_label3);
    update_label_color(nd_label4);
    update_label_color(nd_label5);
  }

  void update_peaks(){
    struct Tracker_Windows *window=root->song->tracker_windows;
    window->wblock->block->is_dirty = true;
    
#if !USE_OPENGL
    struct Tracker_Windows *window=root->song->tracker_windows;
    TRACKREALLINES_update_peak_tracks(window,_patch);
    struct WBlocks *wblock=window->wblock;
    DrawUpAllPeakWTracks(window,wblock,_patch);
#endif
  }

  void onoff_toggled(int voicenum,bool val){
    printf("%d set to %d\n",voicenum,val);
    if(val==true)
      PATCH_turn_voice_on(_patch, voicenum);
    else
      PATCH_turn_voice_off(_patch, voicenum);

    update_peaks();
  }

  void set_transpose(int voicenum){
    float transpose=get_t(voicenum)->value();
    if(transpose!=_voices[voicenum].transpose){
      ADD_UNDO(PatchVoice_CurrPos(_patch,voicenum));
      PATCH_change_voice_transpose(_patch, voicenum, transpose);
    }
    set_editor_focus();
    update_peaks();
  }

  void set_volume(int voicenum){
    float volume=get_v(voicenum)->value();
    if(_voices[voicenum].volume != volume){
      ADD_UNDO(PatchVoice_CurrPos(_patch,voicenum));
      _voices[voicenum].volume = volume;
    }
    set_editor_focus();
    update_peaks();
  }

  void set_start(int voicenum){
    float start=get_s(voicenum)->value();
    if(_voices[voicenum].start != start){
      ADD_UNDO(PatchVoice_CurrPos(_patch,voicenum));
      _voices[voicenum].start = start;
    }
    set_editor_focus();
    update_peaks();
  }

  void set_length(int voicenum){
    float length=get_l(voicenum)->value();
    if(_voices[voicenum].length != length){
      ADD_UNDO(PatchVoice_CurrPos(_patch,voicenum));
      _voices[voicenum].length = length;
    }
    set_editor_focus();
    update_peaks();
  }

  void set_chance(int voicenum){
    int chance=get_c(voicenum)->value();
    if(_voices[voicenum].chance != chance){
      ADD_UNDO(PatchVoice_CurrPos(_patch,voicenum));
      safe_float_write(&_voices[voicenum].chance, chance);
    }
    set_editor_focus();
    update_peaks();
  }
  
public slots:

  void on_o1_toggled(bool val){onoff_toggled(0,val);}
  void on_o2_toggled(bool val){onoff_toggled(1,val);}
  void on_o3_toggled(bool val){onoff_toggled(2,val);}
  void on_o4_toggled(bool val){onoff_toggled(3,val);}
  void on_o5_toggled(bool val){onoff_toggled(4,val);}
  void on_o6_toggled(bool val){onoff_toggled(5,val);}
  void on_o7_toggled(bool val){onoff_toggled(6,val);}

#if 0
  void on_t1_valueChanged(int val){printf("t1 value changed\n");set_transpose(0,val);}
  void on_t2_valueChanged(int val){set_transpose(1,val);}
  void on_t3_valueChanged(int val){set_transpose(2,val);}
  void on_t4_valueChanged(int val){set_transpose(3,val);}
  void on_t5_valueChanged(int val){set_transpose(4,val);}
  void on_t6_valueChanged(int val){set_transpose(5,val);}
  void on_t7_valueChanged(int val){set_transpose(6,val);}
#endif

  void on_t1_editingFinished(){set_transpose(0);}
  void on_t2_editingFinished(){set_transpose(1);}
  void on_t3_editingFinished(){set_transpose(2);}
  void on_t4_editingFinished(){set_transpose(3);}
  void on_t5_editingFinished(){set_transpose(4);}
  void on_t6_editingFinished(){set_transpose(5);}
  void on_t7_editingFinished(){set_transpose(6);}

#if 0
  void on_v1_valueChanged(int val){set_volume(0,val);}
  void on_v2_valueChanged(int val){set_volume(1,val);}
  void on_v3_valueChanged(int val){set_volume(2,val);}
  void on_v4_valueChanged(int val){set_volume(3,val);}
  void on_v5_valueChanged(int val){set_volume(4,val);}
  void on_v6_valueChanged(int val){set_volume(5,val);}
  void on_v7_valueChanged(int val){set_volume(6,val);}
#endif

  void on_v1_editingFinished(){set_volume(0);}
  void on_v2_editingFinished(){set_volume(1);}
  void on_v3_editingFinished(){set_volume(2);}
  void on_v4_editingFinished(){set_volume(3);}
  void on_v5_editingFinished(){set_volume(4);}
  void on_v6_editingFinished(){set_volume(5);}
  void on_v7_editingFinished(){set_volume(6);}

#if 0
  void on_s1_valueChanged(double val){set_start(0,val);}
  void on_s2_valueChanged(double val){set_start(1,val);}
  void on_s3_valueChanged(double val){set_start(2,val);}
  void on_s4_valueChanged(double val){set_start(3,val);}
  void on_s5_valueChanged(double val){set_start(4,val);}
  void on_s6_valueChanged(double val){set_start(5,val);}
  void on_s7_valueChanged(double val){set_start(6,val);}
#endif

  void on_s1_editingFinished(){set_start(0);}
  void on_s2_editingFinished(){set_start(1);}
  void on_s3_editingFinished(){set_start(2);}
  void on_s4_editingFinished(){set_start(3);}
  void on_s5_editingFinished(){set_start(4);}
  void on_s6_editingFinished(){set_start(5);}
  void on_s7_editingFinished(){set_start(6);}

#if 0
  void on_l1_valueChanged(double val){set_length(0,val);}
  void on_l2_valueChanged(double val){set_length(1,val);}
  void on_l3_valueChanged(double val){set_length(2,val);}
  void on_l4_valueChanged(double val){set_length(3,val);}
  void on_l5_valueChanged(double val){set_length(4,val);}
  void on_l6_valueChanged(double val){set_length(5,val);}
  void on_l7_valueChanged(double val){set_length(6,val);}
#endif

  void on_l1_editingFinished(){set_length(0);}
  void on_l2_editingFinished(){set_length(1);}
  void on_l3_editingFinished(){set_length(2);}
  void on_l4_editingFinished(){set_length(3);}
  void on_l5_editingFinished(){set_length(4);}
  void on_l6_editingFinished(){set_length(5);}
  void on_l7_editingFinished(){set_length(6);}

  void on_c1_editingFinished(){set_chance(0);}
  void on_c2_editingFinished(){set_chance(1);}
  void on_c3_editingFinished(){set_chance(2);}
  void on_c4_editingFinished(){set_chance(3);}
  void on_c5_editingFinished(){set_chance(4);}
  void on_c6_editingFinished(){set_chance(5);}
  void on_c7_editingFinished(){set_chance(6);}


  void on_name_widget_editingFinished()
  {
    QString new_name = name_widget->text();

    if(new_name == QString(_patch->name)){
      set_editor_focus();
      return;
    }

    if(new_name==""){
      name_widget->setText("pip");
      new_name = "pip";
    }

    printf("Calling Undo patchname. Old name: %s. New name: %s\n",_patch->name,new_name.toUtf8().constData());

    ADD_UNDO(PatchName_CurrPos(_patch));

    //QTabBar *tab_bar = instruments_widget->tabs->tabBar();
    //tab_bar->tab(tab_bar->currentTab())->setText(name_widget->text());
    //instruments_widget->tabs->setTabLabel((QWidget*)this->parent(), new_name);

    // removed
    //instruments_widget->tabs->setTabText(instruments_widget->tabs->currentIndex(), new_name);

    //instruments_widget->tabs->setTabTextLabel(this, new_name);

    {
      struct Tracker_Windows *window = root->song->tracker_windows;
      struct WBlocks *wblock = window->wblock;
      DO_GFX(
             _patch->name = talloc_strdup((char*)new_name.toUtf8().constData());
             g_currpatch->name_is_edited = true;
             DrawAllWTrackHeaders(window,wblock);
             );
      EditorWidget *editor = static_cast<EditorWidget*>(window->os_visual.widget);
      editor->updateEditor();
    }

    if(_patch->instrument==get_audio_instrument()){
      CHIP_update((SoundPlugin*)_patch->patchdata);
      GFX_update_instrument_widget(_patch);
    }

    set_editor_focus();
  }

  void on_through_onoff_toggled(bool val){
    if(val != _patch->forward_events) {
      ADD_UNDO(PatchName_CurrPos(_patch));
      _patch->forward_events = val;
    }
  }
};
