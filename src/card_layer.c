#include "card_layer.h"
#include "defines.h"

#define BARCODE_SUB_BITMAP_COUNT 8
#define NAME_LEN 256
#define VALUE_LEN 32
  
struct CardLayer {
    Layer *layer;
    int barcode_height;
    int barcode_width;
    uint8_t *barcode_data;
    GBitmap *barcode;
    char *value_text;
    TextLayer *value_text_layer;
    char *name_text;
    TextLayer *name_text_layer;
};

static void background_update_proc(Layer *layer, GContext* ctx);

CardLayer *card_layer_create(GRect frame) {
    static const size_t CardLayer_size = sizeof(CardLayer);
    CardLayer *card_layer = malloc(CardLayer_size);
    memset(card_layer, 0, CardLayer_size);
    card_layer->name_text = malloc(NAME_LEN);
    card_layer->value_text = malloc(VALUE_LEN);

    card_layer->layer = layer_create_with_data(frame, sizeof(CardLayer *));
    layer_set_update_proc(card_layer->layer, background_update_proc);
    *(CardLayer **)layer_get_data(card_layer->layer) = card_layer;

    card_layer->name_text_layer = text_layer_create(GRect(0, 0, PEBBLE_WIDTH, NAME_LAYER_HEIGHT));
    text_layer_set_text_color(card_layer->name_text_layer, GColorWhite);
    text_layer_set_background_color(card_layer->name_text_layer, GColorBlack);
    text_layer_set_font(card_layer->name_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_overflow_mode(card_layer->name_text_layer, GTextOverflowModeTrailingEllipsis);
    text_layer_set_text_alignment(card_layer->name_text_layer, GTextAlignmentCenter);
    layer_add_child(card_layer->layer, (Layer *)card_layer->name_text_layer);


    card_layer->value_text_layer = text_layer_create(GRect(0, 115, PEBBLE_WIDTH, VALUE_LAYER_HEIGHT)); // TODO: Fix magic numbers
    text_layer_set_text_color(card_layer->value_text_layer, GColorBlack);
    text_layer_set_background_color(card_layer->value_text_layer, GColorWhite);
    text_layer_set_font(card_layer->value_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_overflow_mode(card_layer->value_text_layer, GTextOverflowModeTrailingEllipsis);
    text_layer_set_text_alignment(card_layer->value_text_layer, GTextAlignmentCenter);
    layer_add_child(card_layer->layer, (Layer *)card_layer->value_text_layer);

    return card_layer;
}

void card_layer_destroy(CardLayer *card_layer) {
    layer_destroy(card_layer->layer);
    if (card_layer->barcode) gbitmap_destroy(card_layer->barcode);
    if (card_layer->barcode_data) free(card_layer->barcode_data);
    if (card_layer->name_text) free(card_layer->name_text);
    text_layer_destroy(card_layer->name_text_layer);
    if (card_layer->value_text) free(card_layer->value_text);
    text_layer_destroy(card_layer->value_text_layer);
    free(card_layer);
}

Layer *card_layer_get_layer(CardLayer *card_layer) {
    return card_layer->layer;
}

static void draw_barcode_matrix(CardLayer *card_layer, GContext* ctx) {
    int16_t origin_x, origin_y, end_x;

    int16_t img_pixels = card_layer->barcode_width * card_layer->barcode_height;

    // name_text_layer and pager_layer need to be subtracted from the pebble height
    // as does twice the WHITE_BORDER
    const int16_t MAX_HEIGHT = PEBBLE_HEIGHT - NAME_LAYER_HEIGHT - PAGER_LAYER_HEIGHT;

    // The mid point between the black border at the top (name_layer) and at the bottom (pager_layer)
    // is not exactly in the middle of the screen
    const int16_t MID_HEIGHT = (PEBBLE_HEIGHT - NAME_LAYER_HEIGHT - PAGER_LAYER_HEIGHT) / 2 + NAME_LAYER_HEIGHT;

    // Non-linear barcodes are scaled to save persistent storage space. Dynamically calculate
    // the maximum integer scaling factor, keep a white border of 1 units around.

    const int16_t SCALING_FACTOR_X = PEBBLE_WIDTH / (card_layer->barcode_width + 2);
    const int16_t SCALING_FACTOR_Y = MAX_HEIGHT / (card_layer->barcode_height + 2);

    // actual scaling factor is the least of x and y scaling, but at least 1
    const int16_t SCALING_FACTOR = MAX( MIN(SCALING_FACTOR_X, SCALING_FACTOR_Y), 1);
  
    origin_x = ( PEBBLE_WIDTH/2 - (SCALING_FACTOR * card_layer->barcode_width)/2 );
    origin_y = ( MID_HEIGHT - (SCALING_FACTOR * card_layer->barcode_height)/2 );
    end_x = card_layer->barcode_width * SCALING_FACTOR + origin_x;
    
    GRect rect = GRect(origin_x, origin_y, SCALING_FACTOR, SCALING_FACTOR);
  
    APP_LOG(APP_LOG_LEVEL_DEBUG, "image size %ix%i, scaling by %i", card_layer->barcode_width, card_layer->barcode_height, SCALING_FACTOR);

    // The comparison part of this loop adds 7 to the barcode width to allow C
    // to ceil the byte count. Since the server will always pad incomplete bytes
    // with 0, this is reasonably safe.
    for (
          int16_t current_byte = IMG_HEADER_OFFSET;
          current_byte < (IMG_HEADER_OFFSET + ( img_pixels + IMG_BIT_SIZE - 1 ) / IMG_BIT_SIZE );
          current_byte++
        ) {

        for (int8_t p = 0; p < 8; p++) {
            if (card_layer->barcode_data[current_byte] & (1 << p)) {
              graphics_fill_rect(ctx, rect, 0, GCornerNone);              
            }

            rect.origin.x += SCALING_FACTOR;
            if (rect.origin.x == end_x ) {
                rect.origin.x = origin_x;
                rect.origin.y += SCALING_FACTOR;
            }
        }
    }
}

static void draw_barcode_linear(CardLayer *card_layer, GContext* ctx) {
    int16_t img_pixels = card_layer->barcode_width;

    // Try to do an integer scale if possible, keep white border of 1 unit on each side
    const int16_t SCALING_FACTOR_X = PEBBLE_WIDTH / (card_layer->barcode_width + 2);
    const int16_t SCALING_FACTOR = MAX( SCALING_FACTOR_X, 1);

    GRect rect = GRect(( PEBBLE_WIDTH/2 - (SCALING_FACTOR * card_layer->barcode_width)/2 ),
                      ( PEBBLE_HEIGHT / 2 - card_layer->barcode_height / 2 ),
                      SCALING_FACTOR, card_layer->barcode_height);
  
    // The comparison part of this loop adds 7 to the barcode width to allow C
    // to ceil the byte count. Since the server will always pad incomplete bytes
    // with 0, this is reasonably safe.
    for (
          int16_t current_byte = IMG_HEADER_OFFSET;
          current_byte < (IMG_HEADER_OFFSET + ( img_pixels + IMG_BIT_SIZE - 1 ) / IMG_BIT_SIZE );
          current_byte++
        ) {

        for (int16_t current_pixel = 0; current_pixel < 8; current_pixel++) {
            if (card_layer->barcode_data[current_byte] & (1 << current_pixel)) {
              graphics_fill_rect(ctx, rect, 0, GCornerNone);
            }
            rect.origin.x += SCALING_FACTOR;
        }
    }
}

static void background_update_proc(Layer *layer, GContext* ctx) {
    CardLayer *card_layer = *(CardLayer **)layer_get_data(layer);
    if (!card_layer->barcode_data) return;

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

    graphics_context_set_fill_color(ctx, GColorBlack);

    // 0x00 defines the image type
    switch (card_layer->barcode_data[0]) {
        case BARCODE_MATRIX:
            draw_barcode_matrix(card_layer, ctx);
            break;
        case BARCODE_LINEAR:
            draw_barcode_linear(card_layer, ctx);
            break;
    }
}

bool card_layer_set_index(CardLayer *card_layer, uint8_t index) {
    index = index + 1;

    uint8_t num_cards = 0;
    persist_read_data(STORAGE_NUMBER_OF_CARDS, &num_cards, sizeof(num_cards));
    if (index >= num_cards) return false;

    // BARCODE DATA
    if (card_layer->barcode) gbitmap_destroy(card_layer->barcode);
    if (card_layer->barcode_data) free(card_layer->barcode_data);

    const uint32_t barcode_data_key = STORAGE_CARD_VALUE(BARCODE_DATA, index);
    int barcode_data_size = persist_get_size(barcode_data_key);
    card_layer->barcode_data = malloc(barcode_data_size);
    persist_read_data(barcode_data_key, card_layer->barcode_data, barcode_data_size);

    GBitmap *barcode = gbitmap_create_with_data(card_layer->barcode_data);
    card_layer->barcode = barcode;

    card_layer->barcode_width = card_layer->barcode_data[1];
    card_layer->barcode_height = card_layer->barcode_data[2];

    layer_mark_dirty(card_layer->layer);

    // NAME
    const uint32_t name_key = STORAGE_CARD_VALUE(NAME, index);
    persist_read_string(name_key, card_layer->name_text, NAME_LEN);

    text_layer_set_text(card_layer->name_text_layer, card_layer->name_text);

    // VALUE
    if (card_layer->barcode_data[0] == BARCODE_LINEAR) {
      const uint32_t value_key = STORAGE_CARD_VALUE(VALUE, index);
      persist_read_string(value_key, card_layer->value_text, VALUE_LEN);

      text_layer_set_text(card_layer->value_text_layer, card_layer->value_text);
      layer_set_hidden((Layer *)card_layer->value_text_layer, false);
    } else {
      layer_set_hidden((Layer *)card_layer->value_text_layer, true);
    }

    return true;
}
