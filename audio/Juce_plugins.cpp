/*
  A Juce plugin SoundPlugin.

  Handles VST and AU plugins/instruments.

  The old VST Plugin system is still available (audio/VSTPlugins.cpp), and is used when loading old songs.

  New songs will use this system instead for vst plugins.
*/

#include <math.h>
#include <string.h>

#include "../common/nsmtracker.h"
#include "../common/patch_proc.h"
#include "../common/PEQ_LPB_proc.h"
#include "../common/PEQ_Signature_proc.h"
#include "../common/PEQ_Beats_proc.h"
#include "../common/visual_proc.h"
#include "../common/player_proc.h"
#include "../common/OS_Player_proc.h"
#include "../crashreporter/crashreporter_proc.h"
#include "../OpenGL/Widget_proc.h"
#include "../midi/midi_i_plugin_proc.h"

#include "../api/api_proc.h"

#include "SoundPlugin.h"
#include "SoundPlugin_proc.h"

#include "SoundPluginRegistry_proc.h"

#include "VST_plugins_proc.h"

#include "../pluginhost/JuceLibraryCode/JuceHeader.h"

#include "../pluginhost/JuceLibraryCode/AppConfig.h"



#  include "vestige/aeffectx.h"  // It should not be a problem to use VESTIGE in this case. It's just used for getting vendor string and product string.


#include "../midi/midi_proc.h"
#include "Juce_plugins_proc.h"


#include "../midi/midi_juce.cpp"



namespace{

  struct PluginWindow;
  struct MyAudioPlayHead;

  struct Listener : public AudioProcessorListener {

    SoundPlugin *plugin;

    Listener(SoundPlugin *plugin) : plugin(plugin) {}
    
    // Receives a callback when a parameter is changed.
    virtual void 	audioProcessorParameterChanged (AudioProcessor *processor, int parameterIndex, float newValue) {
      //printf("parm %d changed to %f\n",parameterIndex,newValue);
      volatile struct Patch *patch = plugin->patch;
      if (patch != NULL)
        ATOMIC_SET(patch->widget_needs_to_be_updated, true);
    }
 
    // Called to indicate that something else in the plugin has changed, like its program, number of parameters, etc.
    virtual void 	audioProcessorChanged (AudioProcessor *processor) {
      //printf("audioProcessorChanged...\n");
    }
 

    //Indicates that a parameter change gesture has started.
    virtual void 	audioProcessorParameterChangeGestureBegin (AudioProcessor *processor, int parameterIndex) {
      //printf("gesture starts for %d\n",parameterIndex);
    }

    
    //Indicates that a parameter change gesture has finished. 
    virtual void 	audioProcessorParameterChangeGestureEnd (AudioProcessor *processor, int parameterIndex) {
      //printf("gesture ends for %d\n",parameterIndex);
    }
  };

  

  struct Data{
    AudioPluginInstance *audio_instance;
    
    PluginWindow *window;

    MyAudioPlayHead *playHead;

    MidiBuffer midi_buffer;
    AudioSampleBuffer buffer;

    Listener listener;

    int num_input_channels;
    int num_output_channels;

    int x;
    int y;
    
    Data(AudioPluginInstance *audio_instance, SoundPlugin *plugin, int num_input_channels, int num_output_channels)
      : audio_instance(audio_instance)
      , window(NULL)
      , buffer(R_MAX(num_input_channels, num_output_channels), RADIUM_BLOCKSIZE)
      , listener(plugin)
      , num_input_channels(num_input_channels)
      , num_output_channels(num_output_channels)
      , x(-1)
      , y(-1)
    {
      audio_instance->addListener(&listener);
      midi_buffer.ensureSize(1024*16);
    }
  };

  struct MyAudioPlayHead : public AudioPlayHead{
    virtual bool getCurrentPosition (CurrentPositionInfo &result) {
      memset(&result, 0, sizeof(CurrentPositionInfo));

      if (ATOMIC_GET(is_starting_up))
        return false;
      
      bool isplaying = is_playing();

#if 0
      struct Blocks *block = isplaying ? pc->block : root->song->tracker_windows->wblock->block;

      if (block==NULL)
        return false;
#endif
      
      result.bpm = RT_LPB_get_current_BPM();
      //printf("result.bpm: %f\n",result.bpm);

      Ratio signature = RT_Signature_get_current_Signature();
      result.timeSigNumerator = signature.numerator;
      result.timeSigDenominator = signature.denominator;
      //printf("%d/%d\n",signature.numerator,signature.denominator);

      result.timeInSamples = pc->start_time;
      result.timeInSeconds = (double)pc->start_time / (double)pc->pfreq;
#if 0
      result.editOriginTime = 0; //result.timeInSeconds;
#endif
      
      result.ppqPosition = RT_LPB_get_beat_position();
      result.ppqPositionOfLastBarStart = g_beat_position_of_last_bar_start;

      result.isPlaying = isplaying;
#if 0
      result.isRecording = false;
      
      result.ppqLoopStart = 0; // fixme (probably nothing to fix. This value is probably only necessary if time jumps back)
      result.ppqLoopEnd = 0; // fixme (same here)

      result.isLooping = false; //pc->playtype==PLAYBLOCK || pc->playtype==PLAYRANGE; (same here)
#endif
      return true;
    }
  };

  static MyAudioPlayHead myAudioPlayHead;

  struct TypeData{
    const wchar_t *file_or_identifier; // used by Juce
    int uid;
    AudioProcessor::WrapperType wrapper_type;
    const char **effect_names; // set the first time the plugin is loaded
  };

  struct ContainerData{
    const wchar_t *file_or_identifier; // used by Juce
    const wchar_t *library_file_full_path; // used by VST_get_uids()
    AudioProcessor::WrapperType wrapper_type;
  };

  struct PluginWindow  : public DocumentWindow {
    Data *data;
    const char *title;
    
    PluginWindow(const char *title, Data *data, AudioProcessorEditor* const editor)
      : DocumentWindow (title,
                        Colours::lightgrey,
                        DocumentWindow::allButtons,
                        true)
      , data(data)
      , title(title)
    {
      this->setAlwaysOnTop(true);
      this->setSize (400, 300);
      this->setUsingNativeTitleBar(true);
      
      this->setContentOwned(editor, true);
      
      if (data->x <= 0 || data->y <= 0) {
        this->centreWithSize (getWidth(), getHeight());
      } else {
        this->setTopLeftPosition(data->x, data->y);
      }

      this->setVisible(true);
    }

    ~PluginWindow(){      
      data->window = NULL;
      V_free((void*)title);
    }

    /*
    void focusOfChildComponentChanged(FocusChangeType cause) override
    {
      printf("\n\n      child focus %s\n\n", hasKeyboardFocus(true) ? "obtained" : "lost");
      if (hasKeyboardFocus(true))
        obtain_keyboard_focus();
      else
        release_keyboard_focus();
    }

    void focusGained(FocusChangeType cause) override
    {
      printf("\n\n    focusGained\n\n");
      //obtain_keyboard_focus();
    }
    
    void focusLost(FocusChangeType cause) override
    {
      printf("\n\n    focusLost\n\n");
      //release_keyboard_focus();
    }
    */
    
    void closeButtonPressed() override
    {
      delete this;      
    }

    void moved() override {
      data->x = getX();
      data->y = getY();
    }

  };

  
#if JUCE_LINUX
  struct JuceThread : public Thread {
    Atomic<int> isInited;

    JuceThread()
      : Thread("Juce dispatcher thread")
    {
      isInited.set(0);
    }

    virtual void run(){
      initialiseJuce_GUI();
      MessageManager::getInstance(); // make sure there is an instance here to avoid theoretical race condition
      isInited.set(1);
      MessageManager::getInstance()->runDispatchLoop(); 
    }
  };
#endif
}

static void buffer_size_is_changed(struct SoundPlugin *plugin, int new_buffer_size){
  Data *data = (Data*)plugin->data;
  data->buffer.setSize(data->buffer.getNumChannels(), new_buffer_size);
}


static void RT_MIDI_send_msg_to_patch_receivers2(struct Patch *patch, MidiMessage message, int64_t seq_time){       
  if (message.isNoteOn()) {
    //printf("Out. Sending note ON %d\n",  message.getNoteNumber());
    note_t note = create_note_t(-1,
                                message.getNoteNumber(),
                                message.getVelocity() / 127.0f,
                                0.0f,
                                message.getChannel()-1);
    RT_PATCH_send_play_note_to_receivers(patch, note, seq_time);
    
  } else if (message.isNoteOff()) {
    //printf("Out. Sending note OFF %d\n",  message.getNoteNumber());
    note_t note = create_note_t(-1,
                                message.getNoteNumber(),
                                0,
                                0.0f,
                                message.getChannel()-1);
    RT_PATCH_send_stop_note_to_receivers(patch, note, seq_time);
  
  } else if (message.isAftertouch()) {
    note_t note = create_note_t(-1,
                                message.getNoteNumber(),
                                message.getAfterTouchValue() / 127.0f,                                
                                0.0f,
                                message.getChannel()-1);
    RT_PATCH_send_change_velocity_to_receivers(patch, note, seq_time);

  } else {
    
    const uint8_t *raw_data = message.getRawData();
    int len = message.getRawDataSize();

    R_ASSERT_RETURN_IF_FALSE(len>=1 && len<=3);

    uint32_t msg;

    if (len==3)
      msg = MIDI_msg_pack3(raw_data[0],raw_data[1],raw_data[2]);
    else if (len==2)
      msg = MIDI_msg_pack2(raw_data[0],raw_data[1]);
    else if (len==1)
      msg = MIDI_msg_pack1(raw_data[0]);
    else
      return;
    
    RT_PATCH_send_raw_midi_message_to_receivers(patch, msg, seq_time);
  }
}

int RT_MIDI_send_msg_to_patch_receivers(struct Patch *patch, void *data, int data_size, int64_t seq_time){
  int num_bytes_used;

  R_ASSERT(data_size>0);
  
  {
    uint8_t *d=(uint8_t*)data;
    if (d[0] < 0x80) {
      RT_message("Illegal value in first byte of MIDI message: %d\n",d[0]);
      return 0;
    }
  }
  
  MidiMessage message(data, data_size, num_bytes_used, 0);
  
  RT_MIDI_send_msg_to_patch_receivers2(patch, message, seq_time);

  return num_bytes_used;
}

static void RT_MIDI_send_msg_to_patch2(struct Patch *patch, MidiMessage message, int64_t seq_time){       
  if (message.isNoteOn()) {
    note_t note = create_note_t(-1,
                                message.getNoteNumber(),
                                message.getVelocity() / 127.0f,
                                0.0f,
                                message.getChannel()-1);

    RT_PATCH_play_note(patch, note, seq_time);
  
  } else if (message.isNoteOff()) {
    note_t note = create_note_t(-1,
                                message.getNoteNumber(),
                                0,
                                0.0f,
                                message.getChannel()-1);
    RT_PATCH_stop_note(patch, note, seq_time);
  
  } else if (message.isAftertouch()) {
    note_t note = create_note_t(-1,
                                message.getNoteNumber(),
                                message.getAfterTouchValue() / 127.0f,                                
                                0.0f,
                                message.getChannel()-1);
    RT_PATCH_change_velocity(patch, note, seq_time);

  } else {
    
    const uint8_t *raw_data = message.getRawData();
    int len = message.getRawDataSize();

    R_ASSERT_RETURN_IF_FALSE(len>=1 && len<=3);

    uint32_t msg;

    if (len==3)
      msg = MIDI_msg_pack3(raw_data[0],raw_data[1],raw_data[2]);
    else if (len==2)
      msg = MIDI_msg_pack2(raw_data[0],raw_data[1]);
    else if (len==1)
      msg = MIDI_msg_pack1(raw_data[0]);
    else
      return;
    
    RT_PATCH_send_raw_midi_message(patch, msg, seq_time);
  }
}

int RT_MIDI_send_msg_to_patch(struct Patch *patch, void *data, int data_size, int64_t seq_time){
  int num_bytes_used;

  R_ASSERT(data_size>0);
  
  {
    uint8_t *d=(uint8_t*)data;
    if (d[0] < 0x80) {
      RT_message("Illegal value in first byte of MIDI message: %d\n",d[0]);
      return 0;
    }
  }
  
  MidiMessage message(data, data_size, num_bytes_used, 0);
  
  RT_MIDI_send_msg_to_patch2(patch, message, seq_time);

  return num_bytes_used;
}


static void RT_process(SoundPlugin *plugin, int64_t time, int num_frames, float **inputs, float **outputs){
  
  Data *data = (Data*)plugin->data;

#if 0
  for(int ch=0; ch<data->num_output_channels ; ch++)
    memset(outputs[ch], 0, sizeof(float)*num_frames);
  return;
#endif

  // 1. Process audio

  AudioPluginInstance *instance = data->audio_instance;
  AudioSampleBuffer &buffer = data->buffer;

  for(int ch=0; ch<data->num_input_channels ; ch++)
    memcpy(buffer.getWritePointer(ch), inputs[ch], sizeof(float)*num_frames);
    
  int pos = CRASHREPORTER_set_plugin_name(plugin->type->name);{
    instance->processBlock(buffer, data->midi_buffer);
  }CRASHREPORTER_unset_plugin_name(pos);
  
  for(int ch=0; ch<data->num_output_channels ; ch++)
    memcpy(outputs[ch], buffer.getReadPointer(ch), sizeof(float)*num_frames);

  
  // 2. Send out midi
  if (!data->midi_buffer.isEmpty()){

    volatile struct Patch *patch = plugin->patch;

    MidiBuffer::Iterator iterator(data->midi_buffer);
    
    RT_PLAYER_runner_lock();{
      MidiMessage message;
      int samplePosition;

      while(iterator.getNextEvent(message, samplePosition)){

#ifndef RELEASE
        if (samplePosition >= num_frames || samplePosition < 0)
          RT_message("The instrument named \"%s\" of type %s/%s\n"
                     "returned illegal sample position: %d",
                     patch==NULL?"<no name>":patch->name,
                     plugin->type->type_name, plugin->type->name,
                     samplePosition
                     );
#endif
        // Make sure samplePosition has a legal value
        if (samplePosition >= num_frames)
          samplePosition = num_frames-1;
        if (samplePosition < 0)
          samplePosition = 0;

        int len = message.getRawDataSize();
        
#ifndef RELEASE
        R_ASSERT(len > 0);
#endif
        
        if (len>=1 && len<=3) { // Filter out sysex messages.
          if (patch != NULL) {          
            int64_t delta_time = PLAYER_get_block_delta_time(pc->start_time+samplePosition);
            
            int64_t radium_time = pc->start_time + delta_time;
            
            RT_MIDI_send_msg_to_patch_receivers2((struct Patch*)patch, message, radium_time);
          }
        }
      }

    }RT_PLAYER_runner_unlock();

    data->midi_buffer.clear();
  }

}

static void play_note(struct SoundPlugin *plugin, int64_t time, note_t note){
  Data *data = (Data*)plugin->data;
  MidiBuffer &buffer = data->midi_buffer;

  //printf("In. Play note %d %d\n",(int)note_num,(int)time);
  MidiMessage message(0x90 | note.midi_channel, (int)note.pitch, R_BOUNDARIES(0, (int)(note.velocity*127), 127));
  buffer.addEvent(message, time);
}

static void set_note_volume(struct SoundPlugin *plugin, int64_t time, note_t note){
  Data *data = (Data*)plugin->data;
  MidiBuffer &buffer = data->midi_buffer;

  int velocity = R_BOUNDARIES(0,note.velocity,127);
  MidiMessage message(0xa0 | note.midi_channel, (int)note.pitch, velocity, 0.0);
  buffer.addEvent(message, time);
}

static void stop_note(struct SoundPlugin *plugin, int64_t time, note_t note){
  Data *data = (Data*)plugin->data;
  MidiBuffer &buffer = data->midi_buffer;

  int ox90 = MIDI_get_use_0x90_for_note_off() ? 0x90 : 0x80;

  //printf("In. Stop note %d %d\n",(int)note_num,(int)time);
  MidiMessage message(ox90 | note.midi_channel, (int)note.pitch, 0, 0.0);
  buffer.addEvent(message, time);
}

static void send_raw_midi_message(struct SoundPlugin *plugin, int64_t block_delta_time, uint32_t msg){
  uint8_t data[3];
  data[0] = MIDI_msg_byte1(msg);
  data[1] = MIDI_msg_byte2(msg);
  data[2] = MIDI_msg_byte3(msg);

  //printf("   Data: %x %x %x (%x)\n",(int)data[0],(int)data[1],(int)data[2], msg);
  
  int num_bytes_used;
  MidiMessage message(data, 3, num_bytes_used, 0);
  
  if (num_bytes_used>0) {
    Data *data = (Data*)plugin->data;
    MidiBuffer &buffer = data->midi_buffer;
    buffer.addEvent(message, block_delta_time);
  }
  
  //  else
  //  RError("Illegal midi msg: %x",msg); // Well, the illegal message could have been created by a third party plugin.
}

static int RT_get_latency(struct SoundPlugin *plugin){
  Data *data = (Data*)plugin->data;
  AudioPluginInstance *instance = data->audio_instance;
  int latency = instance->getLatencySamples();
  //printf("\n\n\n    Created new latency %d\n\n\n", latency);
  return latency;
}

static void set_effect_value(struct SoundPlugin *plugin, int64_t time, int effect_num, float value, enum ValueFormat value_format, FX_when when){
  Data *data = (Data*)plugin->data;

  //if (effect_num==99)
  //  printf("   Juce_plugins.cpp:set_effect_value.   parm %d: %f\n",effect_num,value);
  
#if 1
  // juce::VSTPluginInstance::setParameter obtains the vst lock. That should not be necessary (Radium ensures that we are alone here), plus that it causes glitches in sound.
  // So instead, we call the vst setParameter function directly:
  AEffect *aeffect = (AEffect*)data->audio_instance->getPlatformSpecificData();

  aeffect->setParameter(aeffect, effect_num, value);

#else
  // This causes glitches, especially when doing heavy operations such as opening the gui:
   data->audio_instance->setParameter(effect_num, value);
#endif
}

static float get_effect_value(struct SoundPlugin *plugin, int effect_num, enum ValueFormat value_format){
  Data *data = (Data*)plugin->data;
#if 1
  // juce::VSTPluginInstance::getParameter obtains the vst lock. That should not be necessary (Radium ensures that we are alone here), plus that it causes glitches in sound.
  // So instead, we call the vst getParameter function directly:
  AEffect *aeffect = (AEffect*)data->audio_instance->getPlatformSpecificData();

  float val = aeffect->getParameter(aeffect, effect_num);

  return val;
#else
  return data->audio_instance->getParameter(effect_num);
#endif
}

static void get_display_value_string(SoundPlugin *plugin, int effect_num, char *buffer, int buffersize){
  
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif
  
  Data *data = (Data*)plugin->data;
  
  String l = data->audio_instance->getParameterLabel(effect_num);
  const char *label = l.toRawUTF8();

  if (true) // audio_instance->getParameterText() sometimes crashes. Doing it manually instead.
  {
    char disp[128] = {}; // c++ way of zero-initialization without getting missing-field-initializers warning.
    AEffect *aeffect = (AEffect*)data->audio_instance->getPlatformSpecificData();
    const int effGetParamDisplay = 7;
    aeffect->dispatcher(aeffect,effGetParamDisplay,
			effect_num, 0, (void *) disp, 0.0f);

    if (disp[0]==0){
      snprintf(buffer,buffersize-1,"%f%s",aeffect->getParameter(aeffect,effect_num),label);//plugin->type_data->params[effect_num].label);
    }else{
      snprintf(buffer,buffersize-1,"%s%s",disp,label);
    }
  }
  else {
    String l = data->audio_instance->getParameterText(effect_num, buffersize-1);
    snprintf(buffer,buffersize-1,"%s%s",l.toRawUTF8(),label);
  }
}

static bool gui_is_visible(struct SoundPlugin *plugin){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  Data *data = (Data*)plugin->data;

  if (data->window==NULL)
    return false;
  else
    return data->window->isVisible();
}

static void show_gui(struct SoundPlugin *plugin){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  Data *data = (Data*)plugin->data;

  GL_lock();{

    if (data->window==NULL) {

      AudioProcessorEditor *editor = data->audio_instance->createEditor(); //IfNeeded();
      
      if (editor != NULL) {

        const char *title = V_strdup(plugin->patch==NULL ? talloc_format("%s %s",plugin->type->type_name, plugin->type->name) : plugin->patch->name);
        data->window = new PluginWindow(title, data, editor);
      }

    }
    
  }GL_unlock();

}


static void hide_gui(struct SoundPlugin *plugin){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif
  
  Data *data = (Data*)plugin->data;

  GL_lock();{

    //data->window->setVisible(false);
    delete data->window; // NOTE: data->window is set to NULL in the window destructor. It's hairy, but there's probably not a better way.

  }GL_unlock();
}


static AudioPluginInstance *get_audio_instance(const TypeData *type_data, float sample_rate, int block_size){
  static bool inited=false;

  static AudioPluginFormatManager formatManager;
    
  if (inited==false){
    formatManager.addDefaultFormats();
    inited=true;
  }

  
  //int uid = VST_get_uid(type_data->library_file_full_path);
  //printf("uid: %d\n",uid);
  //getchar();
  //((PluginDescription*)description)->uid = uid;

  //if (uid==-1)
  //  return NULL;
  
  String errorMessage;

  PluginDescription description;
  
  description.fileOrIdentifier = String(type_data->file_or_identifier);
  description.uid = type_data->uid;

  AudioPluginInstance *instance = formatManager.createPluginInstance(description,sample_rate,block_size,errorMessage);
  
  if (instance==NULL){
    GFX_Message(NULL, "Unable to open VST plugin %s: %s\n",description.fileOrIdentifier.toRawUTF8(), errorMessage.toRawUTF8());
    return NULL;
  }

  instance->setPlayHead(&myAudioPlayHead);

  instance->prepareToPlay(sample_rate, block_size);

  return instance;
}

static void set_plugin_type_data(AudioPluginInstance *audio_instance, SoundPluginType *plugin_type){
  TypeData *type_data = (struct TypeData*)plugin_type->data;

  if (audio_instance->hasEditor()==false) {
    plugin_type->show_gui = NULL;
    plugin_type->hide_gui = NULL;
  }
  
  plugin_type->num_inputs = audio_instance->getTotalNumInputChannels();
  plugin_type->num_outputs = audio_instance->getTotalNumOutputChannels();
    
  plugin_type->plugin_takes_care_of_savable_values = true;
  plugin_type->dont_send_effect_values_from_state_into_plugin = true; // Don't need to. Juce takes care of saving and loading all effect parameters (General optimization plus crash fix for buggy CM 505 plugin)
 
  const char *wrapper_info = "";
  
  if (type_data->wrapper_type == AudioProcessor::wrapperType_VST) {
    AEffect *aeffect = (AEffect*)audio_instance->getPlatformSpecificData();
    {
      char vendor[1024] = {0};
      aeffect->dispatcher(aeffect, effGetVendorString, 0, 0, vendor, 0.0f);
      char product[1024] = {0};
      aeffect->dispatcher(aeffect, effGetProductString, 0, 0, product, 0.0f);

      if(strlen(vendor)>0 || strlen(product)>0)
        wrapper_info = talloc_format("Vendor: %s\nProduct: %s\n",vendor,product);
    }
  }

  plugin_type->info = V_strdup(talloc_format("%sAccepts MIDI: %s\nProduces MIDI: %s\n",wrapper_info, audio_instance->acceptsMidi()?"Yes":"No", audio_instance->producesMidi()?"Yes":"No"));
        
  plugin_type->is_instrument = audio_instance->acceptsMidi(); // doesn't seem like this field ("is_instrument") is ever read...

  plugin_type->num_effects = audio_instance->getNumParameters();

  type_data->effect_names = (const char**)V_calloc(sizeof(char*),plugin_type->num_effects);
  for(int i = 0 ; i < plugin_type->num_effects ; i++)
    //  type_data->effect_names[i] = V_strdup(audio_instance->getParameterName(i).toRawUTF8());
    type_data->effect_names[i] = V_strdup(talloc_format("%d: %s",i,audio_instance->getParameterName(i).toRawUTF8()));
}


static void recreate_from_state(struct SoundPlugin *plugin, hash_t *state, bool is_loading){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  Data *data = (Data*)plugin->data;
  
  AudioPluginInstance *audio_instance = data->audio_instance;

  if (HASH_has_key(state, "audio_instance_state")) {
    const char *stateAsString = HASH_get_chars(state, "audio_instance_state");
    MemoryBlock sourceData;
    sourceData.fromBase64Encoding(stateAsString);
    audio_instance->setStateInformation(sourceData.getData(), sourceData.getSize());
  }

  
  if (HASH_has_key(state, "audio_instance_current_program")) {
    int current_program = HASH_get_int(state, "audio_instance_current_program");
    audio_instance->setCurrentProgram(current_program);
  }

  if (HASH_has_key(state, "audio_instance_program_state")){
    const char *programStateAsString = HASH_get_chars(state, "audio_instance_program_state");
    MemoryBlock sourceData;
    sourceData.fromBase64Encoding(programStateAsString);
    
    audio_instance->setCurrentProgramStateInformation(sourceData.getData(), sourceData.getSize());
  }
  
  if (HASH_has_key(state, "x_pos"))
    data->x = HASH_get_int(state, "x_pos");
  
  if (HASH_has_key(state, "y_pos"))
    data->y = HASH_get_int(state, "y_pos");
}


static int num_running_plugins = 0;

static void *create_plugin_data(const SoundPluginType *plugin_type, SoundPlugin *plugin, hash_t *state, float sample_rate, int block_size, bool is_loading){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  TypeData *type_data = (struct TypeData*)plugin_type->data;

  if(isFullVersion()==false && num_running_plugins >= 2){
    GFX_Message(NULL,
                "Using more than 2 VST plugins is only available to subscribers.<p>"
                "Subscribe <a href=\"http://users.notam02.no/~kjetism/radium/download.php\">here</a>.");
    return NULL;
  }

  AudioPluginInstance *audio_instance = get_audio_instance(type_data, sample_rate, block_size);
  if (audio_instance==NULL){
    return NULL;
  }

  PluginDescription description = audio_instance->getPluginDescription();

  //plugin->name = talloc_strdup(description.name.toUTF8());

  Data *data = new Data(audio_instance, plugin, audio_instance->getTotalNumInputChannels(), audio_instance->getTotalNumOutputChannels());
  plugin->data = data;
    
  if(type_data->effect_names==NULL)
    set_plugin_type_data(audio_instance,(SoundPluginType*)plugin_type); // 'plugin_type' was created here (by using calloc), so it can safely be casted into a non-const.
  
  if (state!=NULL)
    recreate_from_state(plugin, state, is_loading);

  num_running_plugins++;

  return data;
}


static void create_state(struct SoundPlugin *plugin, hash_t *state){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif
  
  Data *data = (Data*)plugin->data;
  
  AudioPluginInstance *audio_instance = data->audio_instance;

  // save state
  {
    MemoryBlock destData;
    audio_instance->getStateInformation(destData);

    if (destData.getSize() > 0){
      String stateAsString = destData.toBase64Encoding();    
      HASH_put_chars(state, "audio_instance_state", stateAsString.toRawUTF8());
    }
  }

  // save program state
  {
    MemoryBlock destData;
    audio_instance->getCurrentProgramStateInformation(destData);

    if (destData.getSize() > 0){
      String stateAsString = destData.toBase64Encoding();    
      HASH_put_chars(state, "audio_instance_program_state", stateAsString.toRawUTF8());
    }
  }

  HASH_put_int(state, "audio_instance_current_program", audio_instance->getCurrentProgram());

  HASH_put_int(state, "x_pos", data->x);
  HASH_put_int(state, "y_pos", data->y);
}

static void cleanup_plugin_data(SoundPlugin *plugin){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  num_running_plugins--;

  printf(">>>>>>>>>>>>>> Cleanup_plugin_data called for %p\n",plugin);
  Data *data = (Data*)plugin->data;

  if (data->window != NULL)
    delete data->window;

  delete data->audio_instance;
  delete data;
}

static const char *get_effect_name(struct SoundPlugin *plugin, int effect_num){
  TypeData *type_data = (struct TypeData*)plugin->type->data;
  return type_data->effect_names[effect_num];
}

/*
static const char *get_effect_description(const struct SoundPluginType *plugin_type, int effect_num){
  TypeData *type_data = (struct TypeData*)plugin_type->data;
  return type_data->effect_names[effect_num];
}
*/

static int get_num_presets(struct SoundPlugin *plugin){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  Data *data = (Data*)plugin->data;
  AudioPluginInstance *instance = data->audio_instance;

  return instance->getNumPrograms();
}

static int get_current_preset(struct SoundPlugin *plugin){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  Data *data = (Data*)plugin->data;
  AudioPluginInstance *instance = data->audio_instance;

  return instance->getCurrentProgram();
}

static void set_current_preset(struct SoundPlugin *plugin, int num){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  Data *data = (Data*)plugin->data;
  AudioPluginInstance *instance = data->audio_instance;

  instance->setCurrentProgram(num);
}

static const char *get_preset_name(struct SoundPlugin *plugin, int num){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  Data *data = (Data*)plugin->data;
  AudioPluginInstance *instance = data->audio_instance;

  return talloc_strdup(instance->getProgramName(num).toRawUTF8());
}

static void set_preset_name(struct SoundPlugin *plugin, int num, const char* new_name){
#if JUCE_LINUX
  const MessageManagerLock mmLock;
#endif

  Data *data = (Data*)plugin->data;
  AudioPluginInstance *instance = data->audio_instance;

  instance->changeProgramName(num, new_name);
}

static SoundPluginType *create_plugin_type(const char *name, int uid, const wchar_t *file_or_identifier, SoundPluginTypeContainer *container){ //, const wchar_t *library_file_full_path){
  printf("b02 %s\n",STRING_get_chars(file_or_identifier));
  fflush(stdout);
  //  return;

  SoundPluginType *plugin_type = (SoundPluginType*)V_calloc(1,sizeof(SoundPluginType));

  TypeData *typeData = (TypeData*)V_calloc(1, sizeof(TypeData));

  typeData->file_or_identifier = wcsdup(file_or_identifier);
  typeData->uid = uid;

  //typeData->library_file_full_path = wcsdup(library_file_full_path);  
  
  typeData->wrapper_type = AudioProcessor::wrapperType_VST;

  plugin_type->data = typeData;

  plugin_type->type_name = "VST";
  plugin_type->name      = V_strdup(name);

  plugin_type->container = container;

  plugin_type->is_instrument = true; // we don't know yet, so we set it to true.
  
  plugin_type->buffer_size_is_changed = buffer_size_is_changed;

  plugin_type->RT_process = RT_process;
  plugin_type->create_plugin_data = create_plugin_data;
  plugin_type->cleanup_plugin_data = cleanup_plugin_data;

  plugin_type->gui_is_visible = gui_is_visible;
  plugin_type->show_gui = show_gui;
  plugin_type->hide_gui = hide_gui;
  
  plugin_type->play_note       = play_note;
  plugin_type->set_note_volume = set_note_volume;
  plugin_type->stop_note       = stop_note;
  plugin_type->send_raw_midi_message = send_raw_midi_message;

  plugin_type->RT_get_latency = RT_get_latency;
  
  plugin_type->get_effect_value = get_effect_value;
  plugin_type->set_effect_value = set_effect_value;
  
  plugin_type->get_display_value_string=get_display_value_string;
  plugin_type->get_effect_name=get_effect_name;
  //plugin_type->get_effect_description=get_effect_description;

  plugin_type->create_state = create_state;
  plugin_type->recreate_from_state = recreate_from_state;

  plugin_type->get_num_presets = get_num_presets;
  plugin_type->get_current_preset = get_current_preset;
  plugin_type->set_current_preset = set_current_preset;
  plugin_type->get_preset_name = get_preset_name;
  plugin_type->set_preset_name = set_preset_name;

  PR_add_plugin_type_no_menu(plugin_type);
  
  return plugin_type;
}

static void populate(SoundPluginTypeContainer *container){
  if (container->is_populated)
    return;

  container->is_populated = true;

  ContainerData *data = (ContainerData*)container->data;

  vector_t *uids = VST_get_uids(data->library_file_full_path);

  int size = uids->num_elements;

  if (size==0)
    return;

  container->num_types = size;
  container->plugin_types = (SoundPluginType**)V_calloc(size, sizeof(SoundPluginType));

  for(int i = 0 ; i < size ; i++){
    radium_vst_uids_t *element = (radium_vst_uids_t*)uids->elements[i];
    const char *name = element->name;
    if (name==NULL)
      name = container->name;
    //fprintf(stderr,"i: %d, %d, name: %s, containername: %s\n",i,name==NULL,name,container->name);fflush(stderr);
    container->plugin_types[i] = create_plugin_type(name, element->uid, data->file_or_identifier, container);
  }

  fflush(stderr);  
}


void add_juce_plugin_type(const char *name, const wchar_t *file_or_identifier, const wchar_t *library_file_full_path){
  SoundPluginTypeContainer *container = (SoundPluginTypeContainer*)V_calloc(1,sizeof(SoundPluginTypeContainer));

  container->type_name = "VST";
  container->name = V_strdup(name);
  container->populate = populate;

  ContainerData *data = (ContainerData*)V_calloc(1, sizeof(ContainerData));
  data->file_or_identifier = wcsdup(file_or_identifier);
  data->library_file_full_path = wcsdup(library_file_full_path);  
  data->wrapper_type = AudioProcessor::wrapperType_VST;

  container->data = data;
  
  PR_add_plugin_container(container);
}


static String g_backtrace;

const char *JUCE_get_backtrace(void){
  g_backtrace = SystemStats::getStackBacktrace();
  return g_backtrace.toUTF8();
}

float JUCE_get_max_val(const float *array, const int num_elements){
  auto both = FloatVectorOperations::findMinAndMax(array,num_elements);
  float a = -both.getStart();
  float b = both.getEnd();
  return R_MAX(a,b);
}


#define TEST_GET_MAX_VAL 0

#if TEST_GET_MAX_VAL

static float RT_get_max_val(const float *array, const int num_elements){
  float ret=0.0f;
  float minus_ret = 0.0f;
  
  for(int i=0;i<num_elements;i++){
    float val = array[i];
    if(val>ret){
      ret=val;
      minus_ret = -val;
    }else if (val<minus_ret){
      ret = -val;
      minus_ret = val;
    }
  }

  return ret;
}

// Found here: http://codereview.stackexchange.com/questions/5143/min-max-function-of-1d-array-in-c-c/
// performance is approx. the same as FloatVectorOperations::findMinAndMax(array,num_elements);
static void x86_sse_find_peaks(const float *buf, unsigned nframes, float *min, float *max)
{
    __m128 current_max, current_min, work;

    // Load max and min values into all four slots of the XMM registers
    current_min = _mm_set1_ps(*min);
    current_max = _mm_set1_ps(*max);

    // Work input until "buf" reaches 16 byte alignment
    while ( ((unsigned long)buf) % 16 != 0 && nframes > 0) {

        // Load the next float into the work buffer
        work = _mm_set1_ps(*buf);

        current_min = _mm_min_ps(current_min, work);
        current_max = _mm_max_ps(current_max, work);

        buf++;
        nframes--;
    }

    // use 64 byte prefetch for quadruple quads
    while (nframes >= 16) {
        //__builtin_prefetch(buf+64,0,0); // for GCC 4.3.2+

        work = _mm_load_ps(buf);
        current_min = _mm_min_ps(current_min, work);
        current_max = _mm_max_ps(current_max, work);
        buf+=4;
        work = _mm_load_ps(buf);
        current_min = _mm_min_ps(current_min, work);
        current_max = _mm_max_ps(current_max, work);
        buf+=4;
        work = _mm_load_ps(buf);
        current_min = _mm_min_ps(current_min, work);
        current_max = _mm_max_ps(current_max, work);
        buf+=4;
        work = _mm_load_ps(buf);
        current_min = _mm_min_ps(current_min, work);
        current_max = _mm_max_ps(current_max, work);
        buf+=4;
        nframes-=16;
    }

    // work through aligned buffers
    while (nframes >= 4) {

        work = _mm_load_ps(buf);

        current_min = _mm_min_ps(current_min, work);
        current_max = _mm_max_ps(current_max, work);

        buf+=4;
        nframes-=4;
    }

    // work through the rest < 4 samples
    while ( nframes > 0) {

        // Load the next float into the work buffer
        work = _mm_set1_ps(*buf);

        current_min = _mm_min_ps(current_min, work);
        current_max = _mm_max_ps(current_max, work);

        buf++;
        nframes--;
    }

    // Find min & max value in current_max through shuffle tricks

    work = current_min;
    work = _mm_shuffle_ps(work, work, _MM_SHUFFLE(2, 3, 0, 1));
    work = _mm_min_ps (work, current_min);
    current_min = work;
    work = _mm_shuffle_ps(work, work, _MM_SHUFFLE(1, 0, 3, 2));
    work = _mm_min_ps (work, current_min);

    _mm_store_ss(min, work);

    work = current_max;
    work = _mm_shuffle_ps(work, work, _MM_SHUFFLE(2, 3, 0, 1));
    work = _mm_max_ps (work, current_max);
    current_max = work;
    work = _mm_shuffle_ps(work, work, _MM_SHUFFLE(1, 0, 3, 2));
    work = _mm_max_ps (work, current_max);

    _mm_store_ss(max, work);
}

static float RT_get_max_val3(const float *array, const int num_elements){
  float a;
  float b;
  x86_sse_find_peaks(array, num_elements, &a, &b);
  return R_MAX(-a,b);
}

extern double monotonic_seconds();

static void testing(void){
  const int num_iterations = 1024*512;
  float test_data[64];

  // test run
  for(int i=0;i<50;i++){
    RT_get_max_val(test_data,64);
    RT_get_max_val2(test_data,64);
    RT_get_max_val3(test_data,64);
  }
  
  double result = 0.0f;
  
  double start_time = monotonic_seconds();
  
  for(int i=0;i<num_iterations;i++){
    result +=
#if 0
      RT_get_max_val(test_data,64);
#else
    RT_get_max_val2(test_data,64);
    //FloatVectorOperations::findMinAndMax(test_data,64).getStart();
    //FloatVectorOperations::findMinimum(test_data,64);
#endif
  }
  
  double end_time = monotonic_seconds();
  double duration = end_time - start_time;

  float a = RT_get_max_val(test_data,64);
  float b = RT_get_max_val2(test_data,64);

  fprintf(stderr,"time: %f (%f), %f %f\n",duration,result,a,b);fflush(stderr);  
}
#endif

void PLUGINHOST_load_fxbp(SoundPlugin *plugin, wchar_t *wfilename){
  Data *data = (Data*)plugin->data;
  AudioPluginInstance *instance = data->audio_instance;

  String filename(wfilename);
  File file(filename);

  MemoryBlock memoryBlock;
  
  bool success = file.loadFileAsData(memoryBlock);
  if (success==false){
    GFX_Message(NULL, "Unable to load %s", STRING_get_chars(wfilename));
    return;
  }
      
  success = VSTPluginFormat::loadFromFXBFile(instance, memoryBlock.getData(), memoryBlock.getSize());
  if (success==false){
    GFX_Message(NULL, "Could not use %s for this plugin", STRING_get_chars(wfilename));
    return;
  }
  
  printf("************** size: %d\n",(int)memoryBlock.getSize());
}
  
static void save_fxbp(SoundPlugin *plugin, wchar_t *wfilename, bool is_fxb){
  Data *data = (Data*)plugin->data;
  AudioPluginInstance *instance = data->audio_instance;

  MemoryBlock memoryBlock;
  bool result = VSTPluginFormat::saveToFXBFile(instance, memoryBlock, is_fxb);
  if (result==false){
    GFX_Message(NULL, "Unable to create FXB/FXP data for this plugin");
    return;
  }
  
  String filename(wfilename);

  File file(filename);

  Result result2 = file.create();

  if (result2.failed()){
    GFX_Message(NULL, "Unable to create file %s (%s)", STRING_get_chars(wfilename), result2.getErrorMessage().toRawUTF8());
    return;
  }
  
  bool result3 = file.replaceWithData(memoryBlock.getData(), memoryBlock.getSize());
  if (result3==false){
    GFX_Message(NULL, "Unable to write data to file %s (disk full?)", STRING_get_chars(wfilename));
    return;
  }
  
  
  printf("\n\n\n ***************** result: %d\n\n\n\n",result);
}

void PLUGINHOST_save_fxb(SoundPlugin *plugin, wchar_t *filename){
  save_fxbp(plugin, filename, true);
}
  
void PLUGINHOST_save_fxp(SoundPlugin *plugin, wchar_t *filename){
  save_fxbp(plugin, filename, false);
}
  
void PLUGINHOST_init(void){
#if TEST_GET_MAX_VAL
  testing();
  exit(0);
#endif

#if 0
  
  // Provoce a very hard crash inside juce:
  
  fprintf(stderr,"init1\n");
  const char crash[] = {3,1,1,1,1,-128,-128,-128,-128,16,1,1,1,1,1,77,26,52,
                        111,4,77,26,52,111,4,1,1,1,1,1,1,0};
  String(CharPointer_UTF8(crash));
  fprintf(stderr,"init2\n");
  
#endif
  
#if JUCE_LINUX // Seems like a separate thread is only necessary on linux.

  JuceThread *juce_thread = new JuceThread;
  juce_thread->startThread();

  while(juce_thread->isInited.get()==0)
    Thread::sleep(20);
  
#else

  initialiseJuce_GUI();

#endif
}
