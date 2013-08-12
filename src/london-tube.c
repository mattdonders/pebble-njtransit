/*
 * London Tube
 * Copyright (C) 2013 Matthew Tole
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"
#include "smallstone.h"
#include "config.h"
#include "http.h"
#include "mini-printf.h"

#if ROCKSHOT
#include "httpcapture.h"
#endif

// See https://gist.github.com/matthewtole/6144056 for explanation.
#if ANDROID
#define MY_UUID { 0x91, 0x41, 0xB6, 0x28, 0xBC, 0x89, 0x49, 0x8E, 0xB1, 0x47, 0xC8, 0x84, 0xF0, 0x17, 0x03, 0x16 }
#else
#define MY_UUID HTTP_UUID
#endif

// Credit for a lot of watch-face code goes to Matthew Tole
// https://github.com/smallstoneapps/london-tube
PBL_APP_INFO(MY_UUID, "NJ Transit", "Matt Donders", 1, 0,  RESOURCE_ID_MENU_ICON, APP_INFO_STANDARD_APP);

// Received variables
#define KEY_NEC_TEXT		10
#define KEY_NEC_DELAY		11
#define KEY_NJCL_TEXT		20
#define KEY_NJCL_DELAY		21
#define KEY_RARV_TEXT		30
#define KEY_RARV_DELAY		31
#define KEY_MNE_TEXT		40
#define KEY_MNE_DELAY		41
#define KEY_MNBN_TEXT		50
#define KEY_MNBN_DELAY		51
#define KEY_BNTN_TEXT		60
#define KEY_BNTN_DELAY		61
#define KEY_PASC_TEXT		70
#define KEY_PASC_DELAY		71
#define KEY_ATLC_TEXT		80
#define KEY_ATLC_DELAY		81
#define KEY_UPDATED			101
	

typedef struct {
  char code[4];
  int status;
  char name[25];
} NJTLine;

char status_temp[2];

static NJTLine lines[] = {
  { "NEC\0", 99, "NE Corridor" },
  { "NJC\0", 99, "NJ Coastline" },
  { "RAR\0", 99, "Raritan Valley" },
  { "MNE\0", 99, "Morris & Essex" },
  { "MNB\0", 99, "Main/Bergen/Port Jervis" },
  { "BNT\0", 99, "Montclair Boonton" },
  { "PAS\0", 99, "Pascack Valley" },
  { "ATL\0", 99, "Atlantic City" }
};

#define NUM_LINES 8

#define HTTP_COOKIE_STATUS 1597854

Window window;
MenuLayer layer_menu;

HeapBitmap menu_icons[4];
#define MENU_ICON_OK 0
#define MENU_ICON_PROBLEM 1
#define MENU_ICON_UNKNOWN 2
#define MENU_ICON_X 3

#define STATE_UPDATING 0
#define STATE_OK 1
#define STATE_ERROR 2
#define STATE_SUCCESS 3
#define STATE_ERRORTWO 4

#define SECTION_LINES 0
#define SECTION_OPTIONS 1

void handle_init(AppContextRef ctx);
void window_load(Window *me);
void window_unload(Window *me);
uint16_t menu_get_num_sections_callback(MenuLayer *me, void *data);
uint16_t menu_get_num_rows_callback(MenuLayer *me, uint16_t section_index, void *data);
int16_t menu_get_header_height_callback(MenuLayer *me, uint16_t section_index, void *data);
void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data);
void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data);
void menu_select_click_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context);
void do_status_request();
NJTLine* get_line_by_code(const char* code);

int xatoi (char** str, long* res);
int state = STATE_UPDATING;

char* line_order = "NECNJCRARMNEMNBBNTPASATL";

void http_reconnect(void* context);
void http_location(float latitude, float longitude, float altitude, float accuracy, void* context);
void http_failure(int32_t cookie, int http_status, void* context);
void http_success(int32_t cookie, int http_status, DictionaryIterator* received, void* context);

void pbl_main(void *params) {

  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .messaging_info = {
      .buffer_sizes = {
        .inbound = 500,
        .outbound = 128,
      }
    }
  };

  #if ROCKSHOT
  http_capture_main(&handlers);
  #endif

  app_event_loop(params, &handlers);
}

void handle_init(AppContextRef ctx) {
  http_set_app_id(87612456);

  resource_init_current_app(&APP_RESOURCES);

  window_init(&window, "NJTransit Window");
  window_stack_push(&window, true);

  create_thanks_window();

  http_register_callbacks((HTTPCallbacks){
    .failure=http_failure,
    .success=http_success,
    .reconnect=http_reconnect
  }, (void*)ctx);

  #if ROCKSHOT
  http_capture_init(ctx);
  #endif

  window_set_window_handlers(&window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });

}

void window_load(Window* me) {

  heap_bitmap_init(&menu_icons[MENU_ICON_OK], RESOURCE_ID_MENU_OK);
  heap_bitmap_init(&menu_icons[MENU_ICON_PROBLEM], RESOURCE_ID_MENU_PROBLEM);
  heap_bitmap_init(&menu_icons[MENU_ICON_UNKNOWN], RESOURCE_ID_MENU_UNKNOWN);
  heap_bitmap_init(&menu_icons[MENU_ICON_X], RESOURCE_ID_MENU_X);

  menu_layer_init(&layer_menu, me->layer.bounds);

  menu_layer_set_callbacks(&layer_menu, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = menu_draw_header_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_click_callback
  });

  menu_layer_set_click_config_onto_window(&layer_menu, me);

  layer_add_child(&me->layer, menu_layer_get_layer(&layer_menu));

  do_status_request();
}

void do_status_request() {
  state = STATE_UPDATING;
  menu_layer_reload_data(&layer_menu);

  DictionaryIterator* body;
  HTTPResult result = http_out_get("http://pebble.mattdonders.com/njtransit/v1/status.php", HTTP_COOKIE_STATUS, &body);
  if (result != HTTP_OK) {
    state = STATE_ERROR;
    menu_layer_reload_data(&layer_menu);
    return;
  }

  result = http_out_send();
  if (result != HTTP_OK) {
    state = STATE_ERROR;
    menu_layer_reload_data(&layer_menu);
  }
}

void window_unload(Window* me) {

  heap_bitmap_deinit(&menu_icons[0]);
  heap_bitmap_deinit(&menu_icons[1]);
  heap_bitmap_deinit(&menu_icons[2]);
  heap_bitmap_deinit(&menu_icons[3]);

}

// Return number of sections for ScrollLayer
uint16_t menu_get_num_sections_callback(MenuLayer *me, void *data) {
  return 2;
}

// Return number of rows per section
// Lines: NUM_LINES & Options: 2 
uint16_t menu_get_num_rows_callback(MenuLayer *me, uint16_t section_index, void *data) {
  
  switch (section_index) {
    case SECTION_LINES:
      return NUM_LINES;
    break;
    
	case SECTION_OPTIONS:
      return 2;
    break; 
  }
	
  return 0;
}

int16_t menu_get_header_height_callback(MenuLayer *me, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  
  switch (section_index) {
    case SECTION_LINES: {
		
      switch (state) {
        case STATE_UPDATING:
          menu_cell_basic_header_draw(ctx, cell_layer, "Updating...");
        break;
        
		case STATE_OK: {
		  char time_str[50];
          PblTm now;
          get_time(&now);
          if (clock_is_24h_style()) {
            string_format_time(time_str, sizeof(time_str), "Updated: %H:%M", &now);
          }
          else {
            string_format_time(time_str, sizeof(time_str), "Updated: %l:%M %p", &now);
          }
		  
          menu_cell_basic_header_draw(ctx, cell_layer, time_str);
        }
        break;
        
		case STATE_ERROR:
          menu_cell_basic_header_draw(ctx, cell_layer, "Updating Failed");
        break;
		
		case STATE_ERRORTWO:
          menu_cell_basic_header_draw(ctx, cell_layer, "Updating Failed!!");
        break;
		  
		case STATE_SUCCESS:
          menu_cell_basic_header_draw(ctx, cell_layer, "UPDATING!");
        break;
      }
    }
    break;
    case SECTION_OPTIONS:
      menu_cell_basic_header_draw(ctx, cell_layer, "Options");
    break;
  }
}

void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  switch (cell_index->section) {
    case 0: {
      char status_label[50];
      GBitmap* bmp;
	  int status_delay = lines[cell_index->row].status;

      switch (status_delay) {
		case 0:
          strcpy(status_label, "No Delays!");
          bmp = &menu_icons[MENU_ICON_OK].bmp;
        break;
		  
		case 98:
          strcpy(status_label, "Canceled!");
          bmp = &menu_icons[MENU_ICON_X].bmp;
        break;
		  
		case 99:
          strcpy(status_label, "Getting Status");
          bmp = &menu_icons[MENU_ICON_UNKNOWN].bmp;
        break;

		default:
		  mini_snprintf(status_label, 11, "%d Minutes", status_delay);
      	  bmp = &menu_icons[MENU_ICON_PROBLEM].bmp;
      }		
	  
		
      menu_cell_basic_draw(ctx, cell_layer, lines[cell_index->row].name, status_label, bmp);
    }
    break;
    
	case 1: {
      switch (cell_index->row) {
        case 0:
          menu_cell_basic_draw(ctx, cell_layer, "Refresh", "Update the status.", NULL);
        break;
       
		case 1:
          menu_cell_basic_draw(ctx, cell_layer, "Say Thanks", "Thanks the developer.", NULL);
        break;
      }
    }
  }
}

void menu_select_click_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  switch (cell_index->section) {
    case 1: {
      switch (cell_index->row) {
        case 0: {
          do_status_request();
          MenuIndex index =  { 0, 0 };
          menu_layer_set_selected_index(menu_layer, index, MenuRowAlignBottom, false);
        }
        break;
        case 1:
          send_thanks("london-tube", "1-0");
          show_thanks_window();
        break;
      }
    }
    break;
  }
}

void http_reconnect(void* context) {
}

void http_failure(int32_t cookie, int http_status, void* context) {
  if (cookie != HTTP_COOKIE_STATUS) {
	state = STATE_ERROR;
	menu_layer_reload_data(&layer_menu);
    return;
  }
  else {
	state = STATE_ERRORTWO;
    menu_layer_reload_data(&layer_menu);
  }
}

void http_success(int32_t cookie, int http_status, DictionaryIterator* received, void* context) {
	switch (cookie) {
    case HTTP_COOKIE_STATUS: {		
      Tuple* tuple_order = dict_find(received, 1);
      Tuple* tuple_statuses = dict_find(received, 2);

      const char* order = tuple_order->value->cstring;
      const char* statuses = tuple_statuses->value->cstring;

      for (int l = 0; l < NUM_LINES; l += 1) {
        char code_str[3];
        code_str[0] = order[(l * 3)];
        code_str[1] = order[(l * 3) + 1];
		code_str[2] = order[(l * 3) + 2];

        NJTLine* line = get_line_by_code(code_str);

        if (strcmp(line->code, code_str) != 0) {
          continue;
        }

		char* status_str = "00";
		status_str[0] = statuses[(l * 2)];
		status_str[1] = statuses[(l * 2) + 1];
		  
		strcpy(status_temp, status_str); 
		  
		long foo;
		xatoi(&status_str, &foo);
		line->status = (int)foo;     
      }
      state = STATE_OK;
      menu_layer_reload_data(&layer_menu);
    }
    break;
  }
}

NJTLine* get_line_by_code(const char* code) {
  for (int l = 0; l < NUM_LINES; l += 1) {
    if (strcmp(lines[l].code, code) == 0) {
      return &lines[l];
    }
  }
  return NULL;
}

/* This function is copied from the Embedded String Functions which
 * is available at http://elm-chan.org/fsw/strf/xprintf.html
 *
 * Since I'm only using it to convert to decimal numbers I should probably
 * rewrite it to make it simpler / more efficient.
 */

int xatoi (     /* 0:Failed, 1:Successful */
  char **str,   /* Pointer to pointer to the string */
  long *res   /* Pointer to the valiable to store the value */
)
{
  unsigned long val;
  unsigned char c, r, s = 0;


  *res = 0;

  while ((c = **str) == ' ') (*str)++;  /* Skip leading spaces */

  if (c == '-') {   /* negative? */
    s = 1;
    c = *(++(*str));
  }

  if (c == '0') {
    c = *(++(*str));
    switch (c) {
    case 'x':   /* hexdecimal */
      r = 16; c = *(++(*str));
      break;
    case 'b':   /* binary */
      r = 2; c = *(++(*str));
      break;
    default:
      if (c <= ' ') return 1; /* single zero */
      if (c < '0' || c > '9') return 0; /* invalid char */
      r = 8;    /* octal */
	}
  } else {
    if (c < '0' || c > '9') return 0; /* EOL or invalid char */
    r = 10;     /* decimal */
  }

  val = 0;
  while (c > ' ') {
    if (c >= 'a') c -= 0x20;
    c -= '0';
    if (c >= 17) {
      c -= 7;
      if (c <= 9) return 0; /* invalid char */
    }
    if (c >= r) return 0;   /* invalid char for current radix */
    val = val * r + c;
    c = *(++(*str));
  }
  if (s) val = 0 - val;     /* apply sign if needed */

  *res = val;
  return 1;
}
