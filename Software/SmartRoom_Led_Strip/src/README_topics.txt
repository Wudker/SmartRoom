MQTT topics
===========

LED1 (efekty):
  smartroom/workshop/led1/set     payload: 1 / 0
  smartroom/workshop/led1/state   payload: 1 / 0 (retained)

LED_BED (oddychanie):
  smartroom/workshop/led_bed/set     payload: 1 / 0
  smartroom/workshop/led_bed/state   payload: 1 / 0 (retained)

OFF nie odcina zasilania. Program wysyla do paska RGB = 0,0,0 przez strip.clear() + strip.show().

Secret_keys.cpp pozostaw lokalnie i dodaj do .gitignore. Secret_keys.h moze zostac w repo.
