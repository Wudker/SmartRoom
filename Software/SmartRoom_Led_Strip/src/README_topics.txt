MQTT topics
===========

LED_shelf:
  smartroom/workshop/led_shelf/set     payload: 1 / 0
  smartroom/workshop/led_shelf/state   payload: 1 / 0 (retained)
LED_brightens:
  smartroom/workshop/led_shelf/brightness   payload: 0-255 (retained)

LED_BED:
  smartroom/workshop/led_bed/set     payload: 1 / 0
  smartroom/workshop/led_bed/state   payload: 1 / 0 (retained)
LED_brightens:
  smartroom/workshop/led_bed/brightness   payload: 0-255 (retained)

Przykład ustawienia jasności LED_BED:
  topic:   smartroom/workshop/led_bed/brightness
  payload: 120

LED_controller_status:
  smartroom/workshop/LED_controller_status payload: 1 / 0 (retained)





