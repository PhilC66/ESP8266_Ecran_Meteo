# esp8266-weather-station-color

ESP8266 Weather Station in Color using ILI9341 TFT 240x320 display

## Hardware Requirements

This code is made for an 240x320 65K ILI9341 display with code running on an ESP8266 feather.
You can buy such a display here: 

https://www.adafruit.com/product/3315
https://www.adafruit.com/product/2821

## Software Requirements/ Libraries

* Arduino IDE with ESP8266 platform installed
* [Weather Station Library](https://github.com/squix78/esp8266-weather-station) or through Library Manager
* [Adafruit ILI9341](https://github.com/adafruit/Adafruit_ILI9341) or through Library Manager
* [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) or through Library Manager
* [WifiManager](https://github.com/tzapu/WiFiManager)

You also need to get an API key for the openweathermap data: https://openweathermap.org

Based on great job from http://blog.squix.ch and https://thingpulse.com, thanks a lot
Difference here are :
managed multi Weather Sation in different selectable city, with attached data from home weather station or not.
icons are record in SPIFFS memory.
move data from weatherground to openweathermap

## Wiring

| ILI9341       | NodeMCU      |
| ------------- |:-------------:| 
| MISO          | -             | 
| LED Backlight | 4	            | 
| SCK           | D5            | 
| MOSI          | D7            |
| DC/RS         | 15            |
| RESET         | RST           |
| CS            | 0             |
| GND           | GND           |
| VCC           | 3V3           |
| STMPE_CS      | 16            |

