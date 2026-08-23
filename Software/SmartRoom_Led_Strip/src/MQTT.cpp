#include "MQTT.h"
#include "LEDs.h"
#include "Secret_keys.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int MQTT_PORT = 1883;
constexpr unsigned long MQTT_RETRY_MS = 5000UL;

const char* TOPIC_AVAILABILITY = "smartroom/workshop/led_controller/availability";

const char* TOPIC_SHELF_SET = "smartroom/workshop/led_shelf/set";
const char* TOPIC_SHELF_STATE = "smartroom/workshop/led_shelf/state";
const char* TOPIC_SHELF_BRIGHTNESS_SET = "smartroom/workshop/led_shelf/brightness/set";
const char* TOPIC_SHELF_BRIGHTNESS_STATE = "smartroom/workshop/led_shelf/brightness/state";
const char* TOPIC_SHELF_CURRENT_EFFECT_STATE = "smartroom/workshop/led_shelf/effect/current/state";

const char* TOPIC_BED_SET = "smartroom/workshop/led_bed/set";
const char* TOPIC_BED_STATE = "smartroom/workshop/led_bed/state";
const char* TOPIC_BED_BRIGHTNESS_SET = "smartroom/workshop/led_bed/brightness/set";
const char* TOPIC_BED_BRIGHTNESS_STATE = "smartroom/workshop/led_bed/brightness/state";
const char* TOPIC_BED_COLOR_SET = "smartroom/workshop/led_bed/color/set";
const char* TOPIC_BED_COLOR_STATE = "smartroom/workshop/led_bed/color/state";
const char* TOPIC_BREATH_PERIOD_SET = "smartroom/workshop/led_bed/breath_period_s/set";
const char* TOPIC_BREATH_PERIOD_STATE = "smartroom/workshop/led_bed/breath_period_s/state";

struct EffectTopics {
    ShelfEffect effect;
    const char* set_topic;
    const char* state_topic;
};

const EffectTopics EFFECT_TOPICS[SHELF_EFFECT_COUNT] = {
    {ShelfEffect::COMET,
     "smartroom/workshop/led_shelf/effect/comet/set",
     "smartroom/workshop/led_shelf/effect/comet/state"},
    {ShelfEffect::RAINBOW,
     "smartroom/workshop/led_shelf/effect/rainbow/set",
     "smartroom/workshop/led_shelf/effect/rainbow/state"},
    {ShelfEffect::REACTOR_GATE,
     "smartroom/workshop/led_shelf/effect/reactor_gate/set",
     "smartroom/workshop/led_shelf/effect/reactor_gate/state"},
    {ShelfEffect::ARC_REACTOR,
     "smartroom/workshop/led_shelf/effect/arc_reactor/set",
     "smartroom/workshop/led_shelf/effect/arc_reactor/state"},
    {ShelfEffect::REPULSOR_CHARGE,
     "smartroom/workshop/led_shelf/effect/repulsor_charge/set",
     "smartroom/workshop/led_shelf/effect/repulsor_charge/state"},
    {ShelfEffect::SUIT_BOOT,
     "smartroom/workshop/led_shelf/effect/suit_boot/set",
     "smartroom/workshop/led_shelf/effect/suit_boot/state"},
    {ShelfEffect::BREATH,
     "smartroom/workshop/led_shelf/effect/breath/set",
     "smartroom/workshop/led_shelf/effect/breath/state"}
};

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);
unsigned long last_mqtt_attempt = 0;

struct StateCache {
    bool valid;
    bool shelf_enabled;
    bool bed_enabled;
    uint8_t shelf_brightness;
    uint8_t bed_brightness;
    uint32_t bed_color;
    uint8_t breath_period;
    ShelfEffect current_effect;
    bool effects[SHELF_EFFECT_COUNT];
};

StateCache state_cache = {};

bool read_on_off_payload(const byte* payload, unsigned int length, bool& value)
{
    if (length != 1 || (payload[0] != '0' && payload[0] != '1')) {
        return false;
    }

    value = payload[0] == '1';
    return true;
}

bool read_uint_payload(const byte* payload, unsigned int length,
                       unsigned long max_value, uint8_t& value)
{
    if (length == 0 || length > 3) {
        return false;
    }

    char buffer[4] = {};
    for (unsigned int i = 0; i < length; ++i) {
        if (!isdigit(static_cast<unsigned char>(payload[i]))) {
            return false;
        }
        buffer[i] = static_cast<char>(payload[i]);
    }

    const unsigned long parsed = strtoul(buffer, nullptr, 10);
    if (parsed > max_value) {
        return false;
    }

    value = static_cast<uint8_t>(parsed);
    return true;
}

bool read_color_payload(const byte* payload, unsigned int length, uint32_t& rgb)
{
    const unsigned int offset = length == 7 && payload[0] == '#' ? 1 : 0;
    if (length - offset != 6) {
        return false;
    }

    char buffer[7] = {};
    for (unsigned int i = 0; i < 6; ++i) {
        const char value = static_cast<char>(payload[i + offset]);
        if (!isxdigit(static_cast<unsigned char>(value))) {
            return false;
        }
        buffer[i] = value;
    }

    rgb = strtoul(buffer, nullptr, 16);
    return true;
}

bool publish_bool(const char* topic, bool value)
{
    return mqtt_client.publish(topic, value ? "1" : "0", true);
}

bool publish_uint(const char* topic, uint8_t value)
{
    char payload[4];
    snprintf(payload, sizeof(payload), "%u", value);
    return mqtt_client.publish(topic, payload, true);
}

bool publish_color(const char* topic, uint32_t rgb)
{
    char payload[7];
    snprintf(payload, sizeof(payload), "%06lX", static_cast<unsigned long>(rgb));
    return mqtt_client.publish(topic, payload, true);
}

void publish_state_changes(bool force)
{
    const bool shelf_enabled = is_led_shelf_enabled();
    const bool bed_enabled = is_led_bed_enabled();
    const uint8_t shelf_brightness = get_led_shelf_brightness();
    const uint8_t bed_brightness = get_led_bed_brightness();
    const uint32_t bed_color = get_led_bed_color();
    const uint8_t breath_period = get_breath_period_s();
    const ShelfEffect current_effect = get_current_shelf_effect();

    if (force || !state_cache.valid || state_cache.shelf_enabled != shelf_enabled) {
        publish_bool(TOPIC_SHELF_STATE, shelf_enabled);
        state_cache.shelf_enabled = shelf_enabled;
    }
    if (force || !state_cache.valid || state_cache.bed_enabled != bed_enabled) {
        publish_bool(TOPIC_BED_STATE, bed_enabled);
        state_cache.bed_enabled = bed_enabled;
    }
    if (force || !state_cache.valid || state_cache.shelf_brightness != shelf_brightness) {
        publish_uint(TOPIC_SHELF_BRIGHTNESS_STATE, shelf_brightness);
        state_cache.shelf_brightness = shelf_brightness;
    }
    if (force || !state_cache.valid || state_cache.bed_brightness != bed_brightness) {
        publish_uint(TOPIC_BED_BRIGHTNESS_STATE, bed_brightness);
        state_cache.bed_brightness = bed_brightness;
    }
    if (force || !state_cache.valid || state_cache.bed_color != bed_color) {
        publish_color(TOPIC_BED_COLOR_STATE, bed_color);
        state_cache.bed_color = bed_color;
    }
    if (force || !state_cache.valid || state_cache.breath_period != breath_period) {
        publish_uint(TOPIC_BREATH_PERIOD_STATE, breath_period);
        state_cache.breath_period = breath_period;
    }
    if (force || !state_cache.valid || state_cache.current_effect != current_effect) {
        mqtt_client.publish(TOPIC_SHELF_CURRENT_EFFECT_STATE,
                            get_shelf_effect_name(current_effect), true);
        state_cache.current_effect = current_effect;
    }

    for (uint8_t i = 0; i < SHELF_EFFECT_COUNT; ++i) {
        const bool enabled = is_shelf_effect_enabled(EFFECT_TOPICS[i].effect);
        if (force || !state_cache.valid || state_cache.effects[i] != enabled) {
            publish_bool(EFFECT_TOPICS[i].state_topic, enabled);
            state_cache.effects[i] = enabled;
        }
    }

    state_cache.valid = true;
}

void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
    bool enabled = false;

    if (strcmp(topic, TOPIC_SHELF_SET) == 0) {
        if (read_on_off_payload(payload, length, enabled)) {
            set_led_shelf_enabled(enabled);
        }
        return;
    }
    if (strcmp(topic, TOPIC_BED_SET) == 0) {
        if (read_on_off_payload(payload, length, enabled)) {
            set_led_bed_enabled(enabled);
        }
        return;
    }

    uint8_t value = 0;
    if (strcmp(topic, TOPIC_SHELF_BRIGHTNESS_SET) == 0) {
        if (read_uint_payload(payload, length, 255, value)) {
            set_led_shelf_brightness(value);
        }
        return;
    }
    if (strcmp(topic, TOPIC_BED_BRIGHTNESS_SET) == 0) {
        if (read_uint_payload(payload, length, 255, value)) {
            set_led_bed_brightness(value);
        }
        return;
    }
    if (strcmp(topic, TOPIC_BREATH_PERIOD_SET) == 0) {
        if (read_uint_payload(payload, length, 10, value)) {
            set_breath_period_s(value);
        }
        return;
    }

    if (strcmp(topic, TOPIC_BED_COLOR_SET) == 0) {
        uint32_t rgb = 0;
        if (read_color_payload(payload, length, rgb)) {
            set_led_bed_color(rgb);
        }
        return;
    }

    for (const EffectTopics& effect_topics : EFFECT_TOPICS) {
        if (strcmp(topic, effect_topics.set_topic) == 0) {
            if (read_on_off_payload(payload, length, enabled) &&
                !set_shelf_effect_enabled(effect_topics.effect, enabled)) {
                Serial.println("At least one shelf effect must remain enabled.");
            }
            return;
        }
    }

    Serial.println("Unknown MQTT topic or invalid payload.");
}

void subscribe_topics()
{
    mqtt_client.subscribe(TOPIC_SHELF_SET);
    mqtt_client.subscribe(TOPIC_BED_SET);
    mqtt_client.subscribe(TOPIC_SHELF_BRIGHTNESS_SET);
    mqtt_client.subscribe(TOPIC_BED_BRIGHTNESS_SET);
    mqtt_client.subscribe(TOPIC_BED_COLOR_SET);
    mqtt_client.subscribe(TOPIC_BREATH_PERIOD_SET);

    for (const EffectTopics& effect_topics : EFFECT_TOPICS) {
        mqtt_client.subscribe(effect_topics.set_topic);
    }
}

void connect_mqtt()
{
    last_mqtt_attempt = millis();

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        return;
    }

    if (!mqtt_client.connect("SmartRoom_LED_STRIPS",
                             TOPIC_AVAILABILITY, 1, true, "offline")) {
        return;
    }

    subscribe_topics();
    mqtt_client.publish(TOPIC_AVAILABILITY, "online", true);
    state_cache.valid = false;
    publish_state_changes(true);
}

} // namespace

void mqtt_begin()
{
    mqtt_client.setServer(mqttBroker_ip, MQTT_PORT);
    mqtt_client.setCallback(mqtt_callback);
    mqtt_client.setBufferSize(512);
    connect_mqtt();
}

void mqtt_loop()
{
    if (!mqtt_client.connected()) {
        if (millis() - last_mqtt_attempt >= MQTT_RETRY_MS) {
            connect_mqtt();
        }
        return;
    }

    mqtt_client.loop();
    publish_state_changes(false);
}
