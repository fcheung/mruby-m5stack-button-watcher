#ifdef M5STICKC
#include <M5StickC.h>
#else
#include <M5Stack.h>
#endif

#include "mruby.h"
#include "mruby/data.h"
#include "mruby/class.h"
#include "mruby/variable.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct ButtonEvent {
  uint32_t when; /*time in ms the event occurred at*/
  uint32_t last_change; /*when did the previous change happen? i.e. if the button was just releasd, when was it pressed*/
  uint8_t pin; /*the pin that was checked*/
  uint8_t pressed;
} ButtonEvent;

static struct RClass* ButtonEventClass = NULL;

typedef struct WatcherContext {
  QueueHandle_t queue; /* where button events get sent */
  TaskHandle_t task; /*the task polling the button(s) */
  Button **buttons; /* buttons to check*/
  uint8_t *pins;
  uint8_t button_count;
} WatcherContext;

static mrb_value mrb_button_event_when(mrb_state *mrb, mrb_value self){
  ButtonEvent *event = (ButtonEvent*)DATA_PTR(self);
  return mrb_fixnum_value(event->when);
}

static mrb_value mrb_button_event_last_change(mrb_state *mrb, mrb_value self){
  ButtonEvent *event = (ButtonEvent*)DATA_PTR(self);
  return mrb_fixnum_value(event->last_change);
}

static mrb_value mrb_button_event_pin(mrb_state *mrb, mrb_value self){
  ButtonEvent *event = (ButtonEvent*)DATA_PTR(self);
  return mrb_fixnum_value(event->pin);
}

static mrb_value mrb_button_event_pressed_p(mrb_state *mrb, mrb_value self){
  ButtonEvent *event = (ButtonEvent*)DATA_PTR(self);
  return mrb_bool_value(event->pressed);
}

static mrb_value mrb_button_event_released_p(mrb_state *mrb, mrb_value self){
  ButtonEvent *event = (ButtonEvent*)DATA_PTR(self);
  return mrb_bool_value(!event->pressed);
}


static void cleanup_task_context(mrb_state *mrb, void *data){
  WatcherContext *context = (WatcherContext*) data;
  
  if(data){
    if(context->buttons){
      for(uint8_t i=0; i < context->button_count; i++){
        delete context->buttons[i];
      }
      mrb_free(mrb, context->buttons);
      context->buttons = NULL;
    }
    if(context->pins){
      mrb_free(mrb, context->pins);
      context->pins = NULL;
    }
    

    if(context->queue){
      vQueueDelete(context->queue);
      context->queue = NULL;
    }
    if(context->task){
      vTaskDelete(context->task);
      context->task = NULL;
    }

    mrb_free(mrb, data);
  }
}

static const struct mrb_data_type watcher_context_state_type = {
  "WatcherContext", cleanup_task_context,
};

static const struct mrb_data_type button_event_type = {
  "ButtonEvent", mrb_free,
};


static void button_watcher_event_task(void *params){
  WatcherContext *context = (WatcherContext*)params;
  while(1) {
    for(int i=0; i < context->button_count; i++){
      Button *button = context->buttons[i];
      button->read();

      if(button->wasPressed() || button ->wasReleased()){
        ButtonEvent ev = {
          millis(),
          button->lastChange(),
          context->pins[i],
          button->isPressed()
        };
        xQueueSendToBack(context->queue, &ev, 10);
      }
    }
    vTaskDelay(10/portTICK_PERIOD_MS);
  }
}

/* start the task that will start polling the button pins*/
static mrb_value mrb_button_watcher_start(mrb_state *mrb, mrb_value self){
  WatcherContext *context = (WatcherContext*)DATA_PTR(self);
  if(!context->task){
    xTaskCreate(button_watcher_event_task, "BWPoller", 1024, context, tskIDLE_PRIORITY, &context->task);
    if(!context->task){
      mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to start watcher task");
    }
  }
  return mrb_nil_value();
}

/* stop the task polls pins*/
static mrb_value mrb_button_watcher_stop(mrb_state *mrb, mrb_value self){
  WatcherContext *context = (WatcherContext*)DATA_PTR(self);
  if(context->task){
    vTaskDelete(context->task);
    context->task = NULL;
  }
  return mrb_nil_value();
}

/* block until the next event is received (or the timeout expires) */
static mrb_value mrb_button_watcher_wait_next_event(mrb_state *mrb, mrb_value self){
  WatcherContext *context = (WatcherContext*)DATA_PTR(self);
  ButtonEvent *event = (ButtonEvent*)mrb_calloc(mrb, 1, sizeof(ButtonEvent));

  mrb_int timeout;
  mrb_get_args(mrb, "i", &timeout);

  if(xQueueReceive(context->queue, event, timeout / portTICK_PERIOD_MS)){
    return mrb_obj_value(Data_Wrap_Struct(mrb, ButtonEventClass, &button_event_type, event));
  }
  else {
    mrb_free(mrb, event);
    return mrb_nil_value();
  }
}


/*initialize a watcher. Allocates a WatcherContext and the releated queue, data structures etc */
static mrb_value mrb_button_watcher_init(mrb_state *mrb, mrb_value self){

  mrb_value kw_debounce;
  uint32_t debounce_ms;
  const char *kw_names[1]= {"debounce"};
  mrb_kwargs kwargs = {
    1,
    &kw_debounce,
    kw_names,
    0,
    NULL
  };
  mrb_value *pins;
  mrb_int pin_count;

  mrb_get_args(mrb, "a:", &pins, &pin_count,&kwargs);
  if(mrb_undef_p(kw_debounce)){
    debounce_ms = 10;
  }else{
    debounce_ms = mrb_fixnum(kw_debounce);
  }

  if(pin_count <= 0){
    mrb_raise(mrb, E_ARGUMENT_ERROR, "At least 1 pin must be specified");
  }
  if(pin_count >= 256){
    mrb_raise(mrb, E_ARGUMENT_ERROR, "At most 256 pins can be specified");
  }

  WatcherContext *context = (WatcherContext*)DATA_PTR(self);

  /* initialize can in theory be called multiple times*/
  if(context) {
    mrb_free(mrb, context);
  }
  mrb_data_init(self, NULL, &watcher_context_state_type);

  context = (WatcherContext*)mrb_calloc(mrb, 1, sizeof(WatcherContext));
  mrb_data_init(self, context, &watcher_context_state_type);
  context->button_count = pin_count;

  context->buttons = (Button**) mrb_calloc(mrb, pin_count, sizeof(Button*));
  context->pins = (uint8_t*) mrb_calloc(mrb, pin_count, sizeof(uint8_t));
  for(mrb_int i = 0; i < pin_count; i++){
    context->pins[i] = mrb_fixnum(pins[i]);
    context->buttons[i] = new Button(context->pins[i], true, debounce_ms);
  }

  context->queue = xQueueCreate(10, sizeof(ButtonEvent));

  if(!context->queue) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to allocate queue.");
  }

  return self;
}



/*start watching for button events. This starts a task that will poll the buttons, post any events that occur*/

/*wait for the next event to occur, or for the timeout to occur. returns either an event or nil */

/*stop watching for button events. This destroys the corresponding task */

void
mrb_mruby_m5stack_button_watcher_gem_init(mrb_state *mrb)
{
  struct RClass *ButtonWatcher  = mrb_define_class(mrb, "ButtonWatcher",  mrb->object_class);
  MRB_SET_INSTANCE_TT(ButtonWatcher, MRB_TT_DATA);


  mrb_define_method(mrb, ButtonWatcher, "initialize", mrb_button_watcher_init, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, ButtonWatcher, "start_watcher", mrb_button_watcher_start, MRB_ARGS_NONE());
  mrb_define_method(mrb, ButtonWatcher, "stop_watcher", mrb_button_watcher_stop, MRB_ARGS_NONE());
  mrb_define_method(mrb, ButtonWatcher, "wait_next_event", mrb_button_watcher_wait_next_event, MRB_ARGS_REQ(1));


  ButtonEventClass  = mrb_define_class_under(mrb, ButtonWatcher, "Event", mrb->object_class);

  mrb_define_method(mrb, ButtonEventClass, "when", mrb_button_event_when, MRB_ARGS_NONE());
  mrb_define_method(mrb, ButtonEventClass, "last_change", mrb_button_event_last_change, MRB_ARGS_NONE());
  mrb_define_method(mrb, ButtonEventClass, "pressed?", mrb_button_event_pressed_p, MRB_ARGS_NONE());
  mrb_define_method(mrb, ButtonEventClass, "released?", mrb_button_event_released_p, MRB_ARGS_NONE());
  mrb_define_method(mrb, ButtonEventClass, "pin", mrb_button_event_pin, MRB_ARGS_NONE());

}

void
mrb_mruby_m5stack_button_watcher_gem_final(mrb_state *mrb)
{
}


#ifdef __cplusplus
}
#endif

