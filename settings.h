/**The MIT License (MIT)
Copyright (c) 2015 by Daniel Eichhorn
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
See more at http://blog.squix.ch
*/

// Parametres site perso
// #define monSite "philippe.co.nf"		//Site internet perso

#include <simpleDSTadjust.h>
// Setup
const int UPDATE_INTERVAL_SECS = 15 * 60;  // Update every 15 minutes
boolean USE_TOUCHSCREEN_WAKE = false;      // use the touchscreen to wake up, ~90mA current draw
boolean DEEP_SLEEP = false;                // use the touchscreen for deep sleep, ~10mA current draw but doesnt work
int     AWAKE_TIME = 5;                    // how many seconds to stay 'awake' before going back to zzz

// Pins for the ILI9341
#define TFT_DC 15
#define TFT_CS 0

// pins for the touchscreen
#define STMPE_CS 16
// #define STMPE_IRQ 4 // pas utilisé

String OPEN_WEATHER_MAP_LANGUAGE = "fr";
const uint8_t MAX_FORECASTS = 10;
const String WDAY_NAMES[] = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
const String MONTH_NAMES[] = {"Janvier", "Fevrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Aout", "Septembre", "Octobre", "Novembre", "Decembre"};
// const String SUN_MOON_TEXT[] = {"Sun", "Rise", "Set", "Moon", "Age", "Illum"};
// const String MOON_PHASES[] = {"New Moon", "Waxing Crescent", "First Quarter", "Waxing Gibbous",
                              // "Full Moon", "Waning Gibbous", "Third quarter", "Waning Crescent"};
// TimeClient settings
// Change for 12 Hour/ 24 hour style clock
bool IS_STYLE_12HR = false;

// const float UTC_OFFSET = 1;// 2 en ete, 1 en hiver
#define UTC_OFFSET +1
struct dstRule StartRule	= {"CEST", Last, Sun, Mar, 2, 3600}; // Central European Summer Time = UTC/GMT +2 hours
struct dstRule EndRule		= {"CET" , Last, Sun, Oct, 2, 0};    // Central European Time = UTC/GMT +1 hour

#define NTP_SERVERS "0.fr.pool.ntp.org", "1.ch.pool.ntp.org", "2.ch.pool.ntp.org"
// #define NTP_SERVERS "us.pool.ntp.org", "time.nist.gov", "pool.ntp.org"

// Wunderground Settings 2x API_KEY pour limiter le nbr de connexions journalieres
// WUNDERGRROUND_API_KEY[0] pour Bompas et Epinal
// WUNDERGRROUND_API_KEY[1] pour Hagetmau
const boolean IS_METRIC = true;
// const String WUNDERGRROUND_API_KEY[2]  = {"895400f07e245903","6918f1cfa03b73fa"};
// const String WUNDERGRROUND_LANGUAGE = "FR";
// const String WUNDERGROUND_COUNTRY   = "FR";
// const String WUNDERGROUND_CITY      = "Hagetmau"; 
// String WUNDERGROUND_CITY      = "zmw:00000.122.07747"; // Bompas 66

//Thingspeak Settings
// const String THINGSPEAK_CHANNEL_ID = "67284";
// const String THINGSPEAK_API_READ_KEY = "L2VIW20QVNZJBLAK";

// const char*  THINGSPEAK_SERVER				= "api.thingspeak.com";
// const String THINGSPEAK_CHANNEL_ID    = "327155";
// const String THINGSPEAK_API_READ_KEY  = "FJYWLE44O7DIMFE5";
// const String THINGSPEAK_API_WRITE_KEY = "SILTKVJM6Y10VKE0";


// List, so that the downloader knows what to fetch
//																			0								1							2						3						4				5					6				7				8					9						10							11						12					13			14			15		16				17					18						19
// String wundergroundIcons [] = {"chanceflurries","chancerain","chancesleet","chancesnow","clear","cloudy","flurries","fog","hazy","mostlycloudy","mostlysunny","partlycloudy","partlysunny","rain","sleet","snow","sunny","tstorms","chancetstorms","unknown"};

const char* getMeteoconIcon(String iconText) {	
	if (iconText == "01d")	return "clear";
	if (iconText == "01n")	return "nt_clear";
	if (iconText == "02d")	return "partlycloudy";
	if (iconText == "02n")	return "nt_partlycloudy";
	if (iconText == "03d" || iconText == "03n") return "cloudy";
	if (iconText == "04d")	return "mostlycloudy";
	if (iconText == "04n")	return "nt_mostlycloudy";
	if (iconText == "09d" || iconText == "09n")	return "sleet";
	if (iconText == "10d" || iconText == "10n")	return "rain";
	if (iconText == "11d" || iconText == "11n")	return "tstorms";
	if (iconText == "13d" || iconText == "13n")	return "snow";
	if (iconText == "50d" || iconText == "50n")	return "fog";
	Serial.print(F("unknown :")),Serial.print(iconText),Serial.print(F("|end"));
	return "unknown";
}

/***************************
 * End Settings
 **************************/

