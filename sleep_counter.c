#include <furi_hal.h>
#include <gui/gui.h>
#include <notification/notification_messages.h>
#include <gui/elements.h>

static const GpioPin* const radarPin = &gpio_ext_pc3; // Pin 7
static const GpioPin* const altRadarPin = &gpio_ext_pa7; // Pin 2
static const GpioPin* const altGroundPin = &gpio_ext_pa6; // Pin 3

bool active = false;
bool altPinout = true; // Sets which GPIO pinout config to use
uint32_t movementCounter = 0; // Counter for movement detections

static void start_feedback(NotificationApp* notifications) {
    // Set LED to red for detection
    notification_message_block(notifications, &sequence_set_only_red_255);
}

static void stop_feedback(NotificationApp* notifications) {
    // Clear LED
    notification_message_block(notifications, &sequence_reset_rgb);
}

static void draw_callback(Canvas* canvas, void* ctx) {
    furi_assert(ctx);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    // Display the new title
    elements_multiline_text_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Sleep Counter");

    // Display movement counter
    char counter_text[32];
    snprintf(counter_text, sizeof(counter_text), "Counter: %lu", movementCounter);
    elements_multiline_text_aligned(canvas, 64, 20, AlignCenter, AlignTop, counter_text);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

static void get_reading() {
    if(altPinout) {
        active = furi_hal_gpio_read(altRadarPin);
    } else {
        active = furi_hal_gpio_read(radarPin);
    }
}

int32_t app_sleep_counter(void* p) {
    UNUSED(p);
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, view_port);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    view_port_update(view_port);

    // Set input to be low; RCWL-0516 outputs High (3v) on detection
    furi_hal_gpio_init(radarPin, GpioModeInput, GpioPullDown, GpioSpeedVeryHigh);
    furi_hal_gpio_init(altRadarPin, GpioModeInput, GpioPullDown, GpioSpeedVeryHigh);
    furi_hal_gpio_init(altGroundPin, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_write(altGroundPin, false);

    // Auto 5v power
    uint8_t attempts = 0;
    bool otg_was_enabled = furi_hal_power_is_otg_enabled();
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();
        furi_delay_ms(10);
    }

    bool alarming = false; // Sensor begins in-active until user starts
    bool running = true;

    while(running) {
        get_reading();

        if(active) {
            if(!alarming) {
                // Presence detected, start feedback and increment counter
                start_feedback(notifications);
                movementCounter++;
            }
            alarming = true;
        } else if(alarming) {
            // No presence detected, stop feedback
            stop_feedback(notifications);
            alarming = false;
        }

        // Exit on back key
        InputEvent event;
        if(furi_message_queue_get(event_queue, &event, 10) == FuriStatusOk) {
            if(event.type == InputTypePress && event.key == InputKeyBack) {
                break;
            }
        }

        // Update display
        view_port_update(view_port);
    }

    // Stop feedback and turn off the LED
    stop_feedback(notifications);

    // Disable 5v power
    if(furi_hal_power_is_otg_enabled() && !otg_was_enabled) {
        furi_hal_power_disable_otg();
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);

    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    return 0;
}
