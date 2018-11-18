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
/* 320x240 */
/* Pin out de l'ESP8266
0 	TFT CS
1 	TX
2 	SD card CS
3 	RX
4		Back Light
5		SD Card detect ou IRQ touch
12	MISO
13	MOSI
14	SCK
15 	TFT DC
16	RT CS Touch Screen
*/

/* recherche mise a jour soft à 03h05 sur site perso*/
/* HUZZAH esp8266	4M(3M SPIFFS)
----------------------to do----------------------------------
securiser extraction data Mameteo bug si message erreur apres les données
il ne faut pas prendre dernier " ou dernier :
// il faut progresser dans les datas jusqu'a la fin
ou utiliser JSON decode



-------------------------------------------------------------*/

/*
V16 14/11/2018 mise a jour librairie WundergroundClient.h, ancienne version dans quarantaine
V15 09/11/2018
ajout icones nuit dans data et prise en compte
V14 28/10/2018
changement timezone dans settings
suppression debug incompatible avec derniere version librairies

V13 22/08/2018 gestion 2xAPI_KEY en fonction ville selectionnée
420352 (40%),45620 (55%)

V12 21/08/2018 correction bug taille affichage lors de la mise à l'heure auto
420192 (40%), 45668 octets (55%)
V10 10/08/2018
420224 octets (40%), 45668 octets (55%) de mémoire dynamique

version simplification nom icones sans telechargement
V100 420940 octets (40%), 45832 octets (55%) de mémoire dynamique
ecran selection ville
V20 20/03/2018 suppression chargement Alert, blocage lors de la mise a jour?
V18 06/11/2017 changement heure dans settings

*/

#include <Arduino.h>
#include <credentials_home.h>
#include <Adafruit_GFX.h>    				// Core graphics library
#include <Adafruit_ILI9341.h> 			// Hardware-specific library
#include <SPI.h>
#include <Wire.h>  									// required even though we do not use I2C 
#include "Adafruit_STMPE610.h"			// touch screen
#include "GfxUi.h"									// Additional UI functions
#include "ArialRoundedMTBold_14.h"	// Fonts created by http://oleddisplay.squix.ch/
#include "ArialRoundedMTBold_36.h"
// #include "WebResource.h"						// Download helper
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>						// Helps with connecting to internet// 
#include <ESP8266httpUpdate.h>    	// Update Over The Air
#include "settings.h"								// check settings.h for adapting to your needs
#include <JsonListener.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>
#include <Astronomy.h>
#include <WundergroundClient.h>			// recuperation info Wunderground
#include "TimeClient.h"							// gestion date heure NTP
// #include <RemoteDebug.h>						// Telnet
#include <EEPROM.h>									// variable en EEPROM
// #include <EEPROMAnything.h>					// variable en EEPROM
// #include <SD.h>	// SD card

#define HOSTNAME "ESP_EcranMeteo"	// HOSTNAME for OTA update
struct config_t								// configuration sauvée en EEPROM
{
	byte 		magic;							// num magique
	byte 		city;								// numero ville
	boolean UseMaMeteo;					// utilise data mameteo ou wunderground false
} config;

const String soft = "ESP8266_E_Meteo.ino.adafruit"; 	// nom du soft
const int 	 ver  = 100;
const byte nbrVille	= 6;
String ville[nbrVille][nbrVille] ={
	{"          ","3014084" ,"3031848","2987914"  ,"3020035","2993728"},
	{"          ","Hagetmau","Bompas" ,"Perpignan","Epinal" ,"Mirecourt"}
};// 0 Weathermap ID , 1 Nom Ville
	
// String WUNDERGROUND_CITY;

float  TensionBatterie; // batterie de l'ecran
String texte;// texte passé pour suppression des car speciaux
String extBmp = ".bmp";

struct t {
float   temp;
float   tempmin;
float   tempmax;
float   humid;
float   pression;
float   rain1h;
float   rain24h;
int 	  derpluie;			// nbr de jour depuis derniere pluie
float   der24h;				// pluie ce dernier jour de pluie
float   pluie7j;
float   pluie30j;
float   pluiemax;
boolean last;					// 1 si >1 heure depuis dernier enregistrement dans la table
float 	vbatt;				// batterie de la station
float   rssi;
String  ssid;
int 		versoft;
String  derjour;
} maMeteo;

// boolean UseMaMeteo = false;// utilsie data mameteo ou wunderground false
byte ecran 					= 0;				// ecran actif
int  zone  					= 0;				// zone de l'ecran
byte frcst 					= 0;				// compteur forecast affiché 
byte nbrecran 			= 4;				// nombre ecran existant

boolean FlagAstronomy = true;
	
long lastDownloadUpdate = millis();
long lastDrew = 0;

#define Ip_Analogique 0					// entree analogique mesure tension
#define Op_BackLight  4					// sortie commande Backlight PWM
// limit de la zone de touché
#define TS_MINX 150
#define TS_MINY 130
#define TS_MAXX 3800
#define TS_MAXY 4000

/*****************************
 * Important: see settings.h to configure your settings!!!
 * ***************************/

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
GfxUi ui = GfxUi(&tft);

Adafruit_STMPE610 spitouch = Adafruit_STMPE610(STMPE_CS);

// WebResource webResource;
// TimeClient timeClient(UTC_OFFSET);

// Set to false, if you prefere imperial/inches, Fahrenheit
// WundergroundClient wunderground(IS_METRIC);


OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
simpleDSTadjust dstAdjusted(StartRule, EndRule);
Astronomy::MoonData moonData;

//declaring prototypes
void configModeCallback (WiFiManager *myWiFiManager);

void updateData();
void drawProgress(uint8_t percentage, String text);
void drawTime();
void drawCurrentWeather();
void drawForecast(byte frcst);
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
String getMeteoconIcon(String iconText);
void drawAstronomy();
void drawCurrentWeatherDetail();
// void drawLabelValue(uint8_t line, String label, String value);
// void drawForecastTable(uint8_t start);
String getTime(time_t *timestamp);
const char* getMeteoconIconFromProgmem(String iconText);
const char* getMiniMeteoconIconFromProgmem(String iconText);
																			// void drawSeparator(uint16_t y);
																			// void sleepNow(int wakeup);
void MesureBatterie();
void GereEcran();
void draw_ecran(byte i);
void MajSoft();
void drawVille();

// RemoteDebug Debug;
//WiFiManager
//Local intialization. Once its business is done, there is no need to keep it around
WiFiManager wifiManager;

WiFiClient client;
//--------------------------------------------------------------------------------//  
void setup() {
	pinMode(Op_BackLight, OUTPUT);
	analogWrite(Op_BackLight,1023);// Backlight ON
  Serial.begin(115200);
	
	//debug.begin("ESP_Meteo_Display");
	// //debug.begin();
  //debug.setResetCmdEnabled(true);
  if (! spitouch.begin()) {
    Serial.println("STMPE not found?");
  }
	// lecture EEPROM
	byte defaultmagic = 121;
	EEPROM.begin(512);
	EEPROM.get(0,config);
	delay(100);
	if(config.magic != defaultmagic){
		config.magic 			= defaultmagic;
		config.city 			= 1;
		config.UseMaMeteo = false;
		// EcrireEEPROM(0);
		EEPROM.put(0,config);
		EEPROM.commit();
		delay(100);
		Serial.print(F("Nouvelle ville : ")),Serial.println(ville[1][config.city]);
	}
	
	// WUNDERGROUND_CITY = ville[0][config.city];
	// Serial.println(WUNDERGROUND_CITY);

  tft.begin();
  tft.fillScreen(ILI9341_BLACK);
  tft.setFont(&ArialRoundedMTBold_14);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(CENTER);
  ui.drawString(120, 160, "Connexion WiFi");

  // Uncomment for testing wifi manager
  //wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);

  //or use this for auto generated name ESP + ChipID
  wifiManager.autoConnect();

  //Manual Wifi
  //WiFi.begin(mySSID, myPASSWORD);

  // OTA Setup
  String hostname(HOSTNAME);
  // hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.begin();
  SPIFFS.begin();
	
	//WiFi.printDiag(Serial);
  
  //Uncomment if you want to update all internet resources
  //SPIFFS.format();

  updateTime();
	updateData();	  // load the weather information
	MesureBatterie();
	
	// Trame_Thingspeak();
	
}
//---------------------------------------------------------------------------
void loop() {
	static byte cpt = 0; // compte nbr passage updateData, à 4 soit 1heure, faire mise a l'heure
	// "standard setup"
	if (!spitouch.bufferEmpty()){				// si ecran touché
		analogWrite(Op_BackLight,1023);		// Backlight ON
		TS_Point p = spitouch.getPoint(); // recupere les coordonnées
		p.x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
		p.y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());
		zone = 0;	
		int x = tft.width() - p.x;
		int y = p.y;
		//debug.print(F("tft.height =")),//debug.print(tft.height());
		//debug.print(F(",px =")),//debug.print(p.x);
		//debug.print(F(",x =")),//debug.print(x);
		//debug.print(F(",y =")),//debug.println(y);
		//	calcul quelle zone a ete touché
		if(y > 0 && y <= 62){//0-60
			Serial.println(F("zone Titre"));
			zone = 1;
		}
		else if (y > 62 && y <= 148){//64-131
			Serial.println(F("zone Actuel"));
			zone = 2;
		}
		else if (y > 148 && y <= 234){
			Serial.println(F("zone forecasts"));//140-210
			zone = 3;				
		}
		else if (y > 234 && y <= 320){//230-320
			Serial.println(F("zone Astronomy"));
			zone = 4;
		}
		if (x > 0   && x <= 80)  zone *=1;
		if (x > 80  && x <= 160) zone *=10;
		if (x > 160 && x <= 240) zone *=100;
		//debug.print(F("zone=")),//debug.println(zone);
		GereEcran();			
		
		//vider buffer
		while(!spitouch.bufferEmpty()){
			TS_Point p = spitouch.getPoint();
		}
	}

	// Check if we should update the clock
	if (millis() - lastDrew > 30000 && timeClient.getSeconds() == "00") {
		//debug.print(timeClient.getHours()),//debug.print(":"),//debug.println(timeClient.getMinutes());
		if(timeClient.getHours() == "03" && timeClient.getMinutes() == "05"){
			//debug.println("Mise a jour");
			// //debug.print(timeClient.getHours()),//debug.print(":"),//debug.println(timeClient.getMinutes());
			MajSoft();
		}
		drawTime();
		lastDrew = millis();
		
		if(JourNuit()){
			Serial.println(F("Jour"));
			analogWrite(Op_BackLight,1023);		// Backlight ON
		}
		else{
			Serial.println(F("Nuit"));
			analogWrite(Op_BackLight,50);			// Backlight OFF
		}			
	}

	// Check if we should update weather information
	if (millis() - lastDownloadUpdate > 1000 * UPDATE_INTERVAL_SECS) {   
		ecran = 0;
		cpt ++;
		if(cpt > 3){	// mise a l'heure tout les 4 passages (1 heure)
			cpt = 0;
			updateTime();
		}
		updateData();
		MesureBatterie();
		lastDownloadUpdate = millis();			
		// Trame_Thingspeak();
	}
		
	ArduinoOTA.handle();	// Handle OTA update requests
	//debug.handle();				// Debug par Telnet
}
//--------------------------------------------------------------------------------//
int moyenneAnalogique(){	// calcul moyenne 10 mesures consécutives
	int moyenne = 0;
  for (int j = 0; j < 10; j++) {
    delay(10);
		moyenne += analogRead(Ip_Analogique);
  }
  moyenne /= 10;
	return moyenne;
}
//--------------------------------------------------------------------------------//
void MesureBatterie(){
	TensionBatterie = map(moyenneAnalogique(), 0, 1023, 0, 11558);
	//debug.print (F("Tension Batterie = ")),//debug.println (TensionBatterie);
	Serial.print(F("Tension Batterie = ")),Serial.println(TensionBatterie);
}
//--------------------------------------------------------------------------------//
// Called if WiFi has not been configured yet
void configModeCallback (WiFiManager *myWiFiManager) {
  ui.setTextAlignment(CENTER);
  tft.setFont(&ArialRoundedMTBold_14);
  tft.setTextColor(ILI9341_CYAN);
  ui.drawString(120, 28, "Wifi Manager");
  ui.drawString(120, 42, "Please connect to AP");
  tft.setTextColor(ILI9341_WHITE);
  ui.drawString(120, 56, myWiFiManager->getConfigPortalSSID());
  tft.setTextColor(ILI9341_CYAN);
  ui.drawString(120, 70, "To setup Wifi Configuration");
	ui.drawString(120, 84, "192.168.4.1");
}
//--------------------------------------------------------------------------------//
void draw_ecran0(){// ecran principal
	tft.fillScreen(ILI9341_BLACK);
  drawTime();
  drawCurrentWeather();
  drawForecast(0);
  if (FlagAstronomy){ 
		drawAstronomy();
	}
	else{
		drawVille();
	}
}
//--------------------------------------------------------------------------------//
void updateTime(){
	String texte = "Maj date heure...";
	Serial.println(texte);
	// debug.println(texte);
	tft.setFont(&ArialRoundedMTBold_14);
  drawProgress(20, texte);
  timeClient.updateTime();
}
//--------------------------------------------------------------------------------//
// Update the internet based information and update screen
void updateData() {
	
	byte API_KEY_Nbr;// selection API_KEY selon ville
	if(config.city == 0){
		API_KEY_Nbr = 0;
	}
	else if(config.city == 1 || config.city == 2){
		API_KEY_Nbr = 1;
	}
	else if(config.city == 3 || config.city == 4){
		API_KEY_Nbr = 2;
	}
	
  tft.fillScreen(ILI9341_BLACK);
  tft.setFont(&ArialRoundedMTBold_14);

	drawProgress(10, "Maj Heure");
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
  while(!time(nullptr)) {
    Serial.print("#");
    delay(100);
  }
  // calculate for time calculation how much the dst class adds.
  dstOffset = UTC_OFFSET * 3600 + dstAdjusted.time(nullptr) - time(nullptr);
  Serial.printf("Time difference for DST: %d\n", dstOffset);
	
  drawProgress(50, "Maj actuelle...");
  OpenWeatherMapCurrent *currentWeatherClient = new OpenWeatherMapCurrent();
  currentWeatherClient->setMetric(IS_METRIC);
  currentWeatherClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient->updateCurrentById(&currentWeather, Openweathermap_key[API_KEY_Nbr],Ville[0][config.city]);
  delete currentWeatherClient;
  currentWeatherClient = nullptr;

  drawProgress(70, "Maj previsions...");
  OpenWeatherMapForecast *forecastClient = new OpenWeatherMapForecast();
  forecastClient->setMetric(IS_METRIC);
  forecastClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12, 0};
  forecastClient->setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient->updateForecastsById(forecasts, Openweathermap_key[API_KEY_Nbr],Ville[0][config.city], MAX_FORECASTS);
  delete forecastClient;
  forecastClient = nullptr;

  drawProgress(80, "Maj astronomie...");
  Astronomy *astronomy = new Astronomy();
  moonData = astronomy->calculateMoonData(time(nullptr));
  float lunarMonth = 29.53;
  moonAge = moonData.phase <= 4 ? lunarMonth * moonData.illumination / 2 : lunarMonth - moonData.illumination * lunarMonth / 2;
  moonAgeImage = String((char) (65 + ((uint8_t) ((26 * moonAge / 30) % 26))));
  delete astronomy;
  astronomy = nullptr;
  delay(1000);	
	
/* 	drawProgress(40, "Maj MaMeteo...");
			
  drawProgress(50, "c");
  wunderground.updateConditions(WUNDERGRROUND_API_KEY[API_KEY_Nbr] , WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  drawProgress(60, "Maj previsions...");
  wunderground.updateForecast(WUNDERGRROUND_API_KEY[API_KEY_Nbr], WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  drawProgress(70, "Maj astronomie...");
  wunderground.updateAstronomy(WUNDERGRROUND_API_KEY[API_KEY_Nbr], WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
	// V20 drawProgress(80, "Maj Alerte...");
  // V20 wunderground.updateAlerts(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  drawProgress(100, "Fin...");
  delay(100);
	draw_ecran(0); */
	
	// Serial.print(F("Vent = ")),Serial.println(wunderground.getWindSpeed());
	// Serial.print(F("gust = ")),Serial.println(wunderground.getWindGust());
	// Serial.print(F("Dir = ")) ,Serial.println(wunderground.getWindDir());
	// debug.print(F("Vent = ")),debug.println(wunderground.getWindSpeed());
	// debug.print(F("gust = ")),debug.println(wunderground.getWindGust());
	// debug.print(F("Dir = ")) ,debug.println(wunderground.getWindDir());	
	// Serial.print("Alerte = "),Serial.println(wunderground.wunderground.getActiveAlertsCnt());
	// Serial.print("Alerte = "),Serial.println(wunderground.getActiveAlertsMessage());
	
}
//--------------------------------------------------------------------------------//
// Progress bar helper
void drawProgress(uint8_t percentage, String text) {
  ui.setTextAlignment(CENTER);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.fillRect(0, 140, 240, 45, ILI9341_BLACK);
  ui.drawString(120, 160, text);
  ui.drawProgressBar(10, 165, 240 - 20, 15, percentage, ILI9341_WHITE, ILI9341_BLUE);
}
//--------------------------------------------------------------------------------//
// draws the clock
void drawTime() {
	tft.fillRect(0, 0, tft.width(), 57,ILI9341_BLACK); // efface existant
  ui.setTextAlignment(CENTER);
  ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setFont(&ArialRoundedMTBold_14);
	Serial.print(F("Date du jour : ")),Serial.print(madate());
	Serial.print(" "),Serial.print(timeClient.getHours());
	Serial.print(":"),Serial.println(timeClient.getMinutes()),
  ui.drawString(120, 20, madate());
  
	// tft.fillRect(0, 27, tft.width(), 31,ILI9341_WHITE); // efface existant
  tft.setFont(&ArialRoundedMTBold_36);
  ui.drawString(120, 56, timeClient.getHours() + ":" + timeClient.getMinutes());
  // drawSeparator(65);
}
//--------------------------------------------------------------------------------//
void drawCurrentWeather() {
	// draws current weather information	
  // Weather Icon	
  String weatherIcon = getMeteoconIcon(wunderground.getTodayIcon());
	
	//------------------------ test --------------------------------
	// String weatherIcon;
	// for(int i=0;i<20;i++){
		// weatherIcon = getMeteoconIcon(wundergroundIcons[i]);
		// ui.drawBmp("/" + weatherIcon + extBmp, 0, 55);//PhC 22/07/2018
		// Serial.print("Icone = "),Serial.print(wunderground.getTodayIcon()),Serial.print(":"),Serial.println(weatherIcon);
		// tft.setFont(&ArialRoundedMTBold_14);
		// ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		// ui.setTextAlignment(RIGHT);
		// ui.drawString(239, 90, wunderground.getWeatherText());
		// RemplaceCharSpec();
		// ui.drawString(239, 90, wundergroundIcons[i]);
		// delay(1000);
		// ui.drawString(239, 90, "                    ");
	// }	
  //------------------------ test --------------------------------
	
	ui.drawBmp("/" + weatherIcon + extBmp, 0, 55);//PhC 22/07/2018
	Serial.print(F("Icone = ")),Serial.print(wunderground.getTodayIcon()),Serial.print(":"),Serial.println(weatherIcon);
  
  // Weather Text
  tft.setFont(&ArialRoundedMTBold_14);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(RIGHT);
  //ui.drawString(239, 90, wunderground.getWeatherText());
	RemplaceCharSpec();
	ui.drawString(239, 90, texte);

  tft.setFont(&ArialRoundedMTBold_36);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(LEFT);//RIGHT
  String degreeSign = "F";
  if (IS_METRIC) {
    degreeSign = "C";
  }
  String tempw = wunderground.getCurrentTemp();// + degreeSign;
  //tempw = "-25.2";
	if(config.UseMaMeteo){
		ui.drawString(110, 125, String(maMeteo.temp,1));
	}
	else{
		ui.drawString(110, 125, tempw);//220 Temperature Wunderground
		// si pas MaMeteo dessin # rouge pour signaler
		// ui.setTextColor(ILI9341_RED, ILI9341_BLACK);
		tft.setFont(&ArialRoundedMTBold_14);
		ui.setTextAlignment(RIGHT);
		ui.drawString(239, 125, "#");
	}
	
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  // maMeteo.tempmax = 45.2;
	if(config.UseMaMeteo){
		if(maMeteo.tempmax > 30) {
			ui.setTextColor(ILI9341_RED, ILI9341_BLACK);//mettre en rouge sinon cyan
		}
		else{
			ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		}
		ui.setTextAlignment(RIGHT);
		ui.drawString(239, 108, String(maMeteo.tempmax,1));//105
	
	// maMeteo.tempmin = -25.5;	
		if(maMeteo.tempmin < 0){
				ui.setTextColor(ILI9341_BLUE, ILI9341_BLACK);	//tempmin
		}
		else{
			ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		}	
		ui.drawString(239, 125, String(maMeteo.tempmin,1));
	}
  // drawSeparator(135);
}
//--------------------------------------------------------------------------------//
void drawForecast(byte seq) {
	// draws the three forecast columns
	// seq = 0,1,2. 3 sequences de 3 jours
	// seq = 0, j = 0  , jour  0, +1, +2
	// seq = 1, j = 6  , jour +3, +4, +5
	// seq = 2, j = 12 , jour +6, +7, +8
	byte j = 0;
	if(seq == 0) j = 0;
	if(seq == 1) j = 6;
	if(seq == 2) j = 12;
	//writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h,uint16_t color) 
	tft.fillRect(0, 153, tft.width(), 80,ILI9341_BLACK); // efface existant

	drawForecastDetail(10 , 165, j);
	drawForecastDetail(95 , 165, j+2);
	drawForecastDetail(180, 165, j+4);
	
  // drawSeparator(165 + 65 + 10);	
}
//--------------------------------------------------------------------------------//
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {
	// helper for the forecast columns
	String weatherIcon = getMeteoconIcon(wunderground.getForecastIcon(dayIndex));
  ui.drawBmp("/mini/" + weatherIcon + extBmp, x, y + 15);
	
	Serial.print(F("miniIcone = ")),Serial.print(wunderground.getForecastIcon(dayIndex)),Serial.print(":"),Serial.println(weatherIcon);
  
	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setFont(&ArialRoundedMTBold_14);
  ui.setTextAlignment(CENTER);
  String day = wunderground.getForecastTitle(dayIndex).substring(0, 3);
  day.toUpperCase();
  ui.drawString(x + 25, y, day);

  ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  ui.drawString(x + 25, y + 14, wunderground.getForecastLowTemp(dayIndex) + "|" + wunderground.getForecastHighTemp(dayIndex));

}
//--------------------------------------------------------------------------------//
void drawVille() { // conditions actuelles sur ville
	tft.fillRect(0, 234, tft.width(), 86,ILI9341_BLACK); // efface existant
	tft.setFont(&ArialRoundedMTBold_14);  
  ui.setTextAlignment(CENTER);
  ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
	String temp;
	ui.drawString(120, 250, ville[1][config.city]);
	
	tft.setFont(&ArialRoundedMTBold_36);  
  ui.setTextAlignment(LEFT);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
	temp = wunderground.getCurrentTemp();
	temp += " C";
	ui.drawString(10, 300, temp);
	ui.setTextAlignment(RIGHT);
	temp = wunderground.getHumidity();
	// temp += " %";
	ui.drawString(230, 300, temp);
	
}
//--------------------------------------------------------------------------------//
void drawAstronomy() {
	tft.fillRect(0, 234, tft.width(), 86,ILI9341_BLACK); // efface existant
	// draw moonphase and sunrise/set and moonrise/set
  int moonAgeImage = 24 * wunderground.getMoonAge().toInt() / 30.0;
	// moonAgeImage = 23;
  ui.drawBmp("/moon" + String(moonAgeImage) + extBmp, 120 - 30, 255);
	  
  // ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setFont(&ArialRoundedMTBold_14);  
  ui.setTextAlignment(LEFT);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.drawString(20, 270, F("Soleil"));
  ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  ui.drawString(20, 285, wunderground.getSunriseTime());
  ui.drawString(20, 300, wunderground.getSunsetTime());
	
	// Serial.print(F("Levé   Soleil :")),Serial.println(wunderground.getSunriseTime());
	// Serial.print(F("Couché Soleil :")),Serial.println(wunderground.getSunsetTime());

  ui.setTextAlignment(RIGHT);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.drawString(220, 270, F("Lune"));
  ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  ui.drawString(220, 285, wunderground.getMoonriseTime());
  ui.drawString(220, 300, wunderground.getMoonsetTime());  
}
//--------------------------------------------------------------------------------//
String getMeteoconIcon(String iconText) {
	// Serial.print("icontext :"),Serial.print(iconText);
	// Serial.print(","),Serial.println(iconText.length());
	if (iconText == "hazy") return "fog";
	if (iconText == "sunny") return "clear";
	if (iconText == "nt_sunny") return "nt_clear";								//V15
	if (iconText == "partlysunny") return "mostlycloudy";
	if (iconText == "nt_partlysunny") return "nt_mostlycloudy";		//V15
	if (iconText == "mostlysunny") return "partlycloudy";
	if (iconText == "nt_mostlysunny") return "nt_partlycloudy";		//V15
	if (iconText.substring(0,3) == "nt_"){
		iconText = iconText.substring(3,iconText.length());
		Serial.print(iconText);
		return iconText;
	}
	if (iconText.length() < 2){
		return "unknown";
	}
	else{
		 return iconText;
	}
/* 	
	// Helper function, should be part of the weather station library and should disappear soon
  if (iconText == "F") return "chanceflurries";	//0
  if (iconText == "Q") return "chancerain";			//1
  if (iconText == "W") return "chancesleet";		//2
  if (iconText == "V") return "chancesnow";			//3
  if (iconText == "S") return "chancetstorms";
  if (iconText == "B") return "clear";					//4 "clear" ou "sunny"
  if (iconText == "Y") return "cloudy";					//5
  if (iconText == "F") return "flurries";				//6
  if (iconText == "M") return "fog";						//7 "fog" ou "hazy"
  // if (iconText == "E") return "hazy";						//8
  if (iconText == "Y") return "mostlycloudy";		//9 "mostlycloudy" ou "partlysunny"
  // if (iconText == "H") return "mostlysunny";		//10
  if (iconText == "H") return "partlycloudy";		//11 "partlycloudy" ou "mostlysunny"
  // if (iconText == "J") return "partlysunny";		//12
  if (iconText == "R") return "rain";						//13
	if (iconText == "W") return "sleet";					//14
	if (iconText == "W") return "snow";						//15
  // if (iconText == "B") return "sunny";					//16
  if (iconText == "0") return "tstorms";				//17

  return "unknown";															//18
	 */
}
//--------------------------------------------------------------------------------//
/* void drawSeparator(uint16_t y) {
	// if you want separators, uncomment the tft-line
	
  // tft.drawFastHLine(10, y, 240 - 2 * 10, 0x4228);
	tft.drawFastHLine(0, y, 200, ILI9341_RED);
} */
//--------------------------------------------------------------------------------//
String madate(){
	String date = wunderground.getDate();
	
	String jour = date.substring(0,3);
	String mois = date.substring(8,11);
	
	if(mois == F("Jan")) mois = F("Janvier");
	if(mois == F("Feb")) mois = F("Fevrier");
	if(mois == F("Mar")) mois = F("Mars");
	if(mois == F("Apr")) mois = F("Avril");
	if(mois == F("May")) mois = F("Mai");
	if(mois == F("Jun")) mois = F("Juin");
	if(mois == F("Jul")) mois = F("Juillet");
	if(mois == F("Aug")) mois = F("Aout");
	if(mois == F("Sep")) mois = F("Septembre");
	if(mois == F("Oct")) mois = F("Octobre");
	if(mois == F("Nov")) mois = F("Novembre");
	if(mois == F("Dec")) mois = F("Decembre");
	
	if(jour == F("Mon")) jour = F("Lundi");
	if(jour == F("Tue")) jour = F("Mardi");
	if(jour == F("Wed")) jour = F("Mercredi");
	if(jour == F("Thu")) jour = F("Jeudi");
	if(jour == F("Fri")) jour = F("Vendredi");
	if(jour == F("Sat")) jour = F("Samedi");
	if(jour == F("Sun")) jour = F("Dimanche");
		
	date = jour +" " + date.substring(4,7) + " " + mois + " " + date.substring(12,16);
	
	return date;
}
//--------------------------------------------------------------------------------//
void RemplaceCharSpec(){
	// Remplacer les caracteres >127
	// 130,136-138 	par e 101
	// 131-134 			par a 97
	// 135 					par c 99
	// si é caracteres recus 195 + 169
	
	texte = String(wunderground.getWeatherText());
	String textec = "";
	// Serial.print(F("texte a convertir =")), Serial.println(texte);
		for(byte i=0; i < texte.length();i++){	
		// Serial.print("i="),Serial.println(i);		
			// Serial.print(texte[i]),Serial.println(int(texte[i]));			
			if((int)texte[i] == 195){//&& (int)texte[i+1] == 169
				textec += char(101);					
				// Serial.println(textec[i]);
				i ++;
			}
			else{
				textec += texte[i];
			}
		}
	texte = textec;
	// Serial.print(F("texte converti =")), Serial.println(texte);
}
//---------------------------------------------------------------------------
boolean JourNuit(){ 
	// determine si jour ou nuit
	// Jour = true, Nuit=false
	
	char *dstAbbrev;
	time_t now = dstAdjusted.time(&dstAbbrev);
	struct tm * timeinfo = localtime (&now);
	time_t timesunrise = currentWeather.sunrise + dstOffset;
	time_t timesunset  = currentWeather.sunset  + dstOffset;
	// Serial.println(now);
	// Serial.println(timesunrise);
	// Serial.printf("%02d:%02d:%02d\n",timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
	// Serial.print(" Sun rise : "),Serial.println(getTime(&timesunrise));
	
	timesunrise -= 3600;	// marge 1 heure
	timesunset  += 3600;
	
	if(timesunset > timesunrise){
		if((now > timesunset && now > timesunrise)
		 ||(now < timesunset && now < timesunrise)){
			// Nuit
			return false;
		}
		else{	// Jour
			return true;
		}
	}
	else{
		if(now > timesunset && now < timesunrise){
		 // Nuit
			return false;
		}
		else{	// Jour
			return true;
		}
	}
}
//----------------------------------------------------------------------------------------------//
void Mameteo(){ // lecture data ma meteo, valide si reponse
	// lecture data ma station méteo, synthese.php
	String data = "getmeteo=r";
	String tempo = "Host:";
	tempo += monSite;
	if (client.connect(monSite, 80)) {
		client.println("POST /meteo/synthese.php HTTP/1.1");
		client.println (tempo);          // SERVER ADDRESS HERE TOO
		client.println("Content-Type: application/x-www-form-urlencoded");
		client.print("Content-Length: ");
		client.println(data.length());
		client.println();
		client.print(data);
		delay(500);	//	V17 necessaire avec box Guillaume
		// Serial.print(F("Envoye au serveur :")),Serial.println(data);
		String req = client.readStringUntil('X');//'\r' ne renvoie pas tous les caracteres?
		
		int pos = req.indexOf("{");
		req = req.substring(pos, req.length());
		Serial.print(F("Reponse serveur :")),Serial.println(req);
		if(req.length() == 0){// si pas de reponse
			config.UseMaMeteo = false;
			return;
		}
		//Serial.println(req.length());
		int pos1=0;
		int pos2=0;
		int i = 1;
		int j = 0;
		String var1;
		// Serial.print("last : = "),Serial.println(req.lastIndexOf(":"));
		do{
			j ++;
			pos1 = req.indexOf(":", i);
			pos2 = req.indexOf(",", pos1+1);
			var1 = req.substring(pos1 + 1, pos2);
			// Serial.print("pos1 = ");
			// Serial.print(pos1);
			// Serial.print(" pos2 = ");
			// Serial.print(pos2);
			// Serial.print(" var = ");
			// Serial.println(var1);
			
			ecrirevaleur(var1, j);
			i = pos1 + 1;//   = req.indexOf(":", pos1 + 1);
		}while(i <= req.lastIndexOf(":"));
		
		String temp =req.substring(req.lastIndexOf(":")+2,req.lastIndexOf("\""));
		// Serial.println(temp);
		maMeteo.derjour = temp.substring(8,10);
		maMeteo.derjour += "-";
		maMeteo.derjour += temp.substring(5,7);
		maMeteo.derjour += "-";
		maMeteo.derjour += temp.substring(0,4);
		// Serial.println(derjour);
	}
		
	if (client.connected()) {   
    client.stop();  // DISCONNECT FROM THE SERVER    
  }
	// dernier enregistrement depuis +1heure, erreur lecture T ou rh
	if(maMeteo.temp == 998 || maMeteo.humid == 998 || maMeteo.last == 1){
		config.UseMaMeteo = false;
	}
	else{
		config.UseMaMeteo = true;
	}
}
//----------------------------------------------------------------------------------------------//
void ecrirevaleur(String var, int j) {
  //////////convertir string en float ou int et copie dans variables//////////////////////////////
  float varfloat = var.toFloat() ; 
  if (j == 1)  maMeteo.temp 		= varfloat;
  if (j == 2)  maMeteo.tempmin 	= varfloat;
  if (j == 3)  maMeteo.tempmax 	= varfloat;
	if (j == 4)  maMeteo.humid 		= varfloat;
	if (j == 5)  maMeteo.pression = varfloat;
	if (j == 6)  maMeteo.rain1h 	= varfloat;
	if (j == 7)  maMeteo.rain24h 	= varfloat;
	if (j == 8)  maMeteo.derpluie = var.toInt();
	if (j == 9)  maMeteo.pluie7j  = varfloat;
	if (j == 10) maMeteo.pluie30j = varfloat;
	if (j == 11) maMeteo.pluiemax = varfloat;
	if (j == 12) maMeteo.der24h   = varfloat;
	if (j == 13) maMeteo.last     = var.toInt();
	if (j == 14) maMeteo.vbatt    = varfloat;
	if (j == 15) maMeteo.rssi    	= varfloat;
	if (j == 16) maMeteo.ssid    	= var;
	if (j == 17) maMeteo.versoft 	= var.toInt();
	
  //debug.print(F("var=")),  //debug.print(j),  //debug.print(";"),  //debug.println(varfloat);
	Serial.print(F("var=")), Serial.print(j), Serial.print(";"),Serial.print(var), Serial.print(";"), Serial.println(varfloat);
}
//----------------------------------------------------------------------------------------------//
void draw_ecran1(){// ecran complement meteo Pluie/Vent
	String temp = "";
	tft.fillScreen(ILI9341_BLACK);
	drawTime();
	int x;
	int y;
	// Zone Pluie
	x=20;
	y=70;//65
	float pluie1h;
	float pluie24h;
	if(config.UseMaMeteo){
		pluie1h  = maMeteo.rain1h;
		pluie24h = maMeteo.rain24h;
	}
	else{
		pluie1h  = wunderground.getPrecipitation1h().toFloat();
		pluie24h = wunderground.getPrecipitationToday().toFloat();
		//debug.print(F("Pluie 1h = ")),//debug.print(wunderground.getPrecipitation1h());
		//debug.print(";"),//debug.println(pluie1h);
		//debug.print(F("Pluie 24h = ")),//debug.print(wunderground.getPrecipitationToday());
		//debug.print(";"),//debug.println(pluie24h);
		
	}
	
	if (pluie1h > 0){		
		ui.drawBmp("/pluie"  + extBmp, x, y);// icone pluie	faible	
	}
	else if(pluie1h > 5){	//	V18
		ui.drawBmp("/pluie2" + extBmp, x, y);// icone pluie	forte
	}
	else{
		ui.drawBmp("/npluie" + extBmp, x, y);// icone pas de pluie 
	}
	
	tft.setFont(&ArialRoundedMTBold_36);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(LEFT);//RIGHT
	temp = String(pluie1h,1);
	ui.drawString(110, 125, temp);	// Rain 1h
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(LEFT);
	temp = String(pluie24h,1);
	ui.drawString(200, 108, temp);	// raIN 24
	ui.drawString(200, 125, "mm");
	
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  ui.setTextAlignment(LEFT);
	
	if(config.UseMaMeteo){
		temp = "";//Der pluie,
		temp += String(maMeteo.der24h,1);
		temp += " mm, ";
		if(maMeteo.derpluie == 0 ){
			temp += "aujourd'hui.";		
		}
		else if(maMeteo.derpluie == 1){
			temp += "hier.";
		}
		else if(maMeteo.derpluie > 1){
			temp += "il y a ";
			temp += String(maMeteo.derpluie);
			temp += " jours.";
		}
		ui.drawString(10, 165, temp);
		
		temp  = "7 derniers jours ";
		temp += String(maMeteo.pluie7j,1);
		temp += " mm.";
		ui.drawString(10, 185, temp);
		
		temp  = "30 derniers jours ";
		temp += String(maMeteo.pluie30j,1);
		temp += " mm.";
		ui.drawString(10, 205, temp);
		
		temp  = "max 30j, ";
		temp += maMeteo.derjour;
		temp += " ";
		temp += String(maMeteo.pluiemax,1);
		temp += " mm.";
		ui.drawString(10, 225, temp);
	}
	x=20;
	y=255;
	// Zone Vent
	String dir = wunderground.getWindDir();
	dir.toUpperCase();
	dir.trim();
	if	(dir == "NORD" || dir == "NORTH"){
		ui.drawBmp("/n" + extBmp , x, y);// icone vent		
	}
	else if (dir == "SUD" || dir == "SOUTH"){
		ui.drawBmp("/s" + extBmp , x, y);// icone vent
	}
	else if (dir == "EST" || dir == "EAST"){
		ui.drawBmp("/e" + extBmp , x, y);// icone vent
	}
	else if (dir == "OUEST" || dir == "WEST"){
		ui.drawBmp("/o" + extBmp , x, y);// icone vent
	}
	else if (dir == "NE" || dir == "NNE"
				|| dir == "ENE"){
		ui.drawBmp("/ne" + extBmp , x, y);
	}
	else if (dir == "SE" || dir == "ESE"
				|| dir == "SSE"){
		ui.drawBmp("/se" + extBmp , x, y);
	}
	else if (dir == "SO" || dir == "SSO"
				|| dir == "OSO" || dir == "SW"
				|| dir == "SSW" || dir == "WSW") {
		ui.drawBmp("/so" + extBmp , x, y);
	}
	else if (dir == "NO" || dir == "NNO"
				|| dir == "ONO" || dir == "NW"
				|| dir == "NNW" || dir == "WNW"){
		ui.drawBmp("/no" + extBmp , x, y);
	}
	else if (dir == "VARIABLE"){
		ui.drawBmp("/variable" + extBmp , x, y);
	}
// Serial.println(dir);
	tft.setFont(&ArialRoundedMTBold_36);
  ui.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  ui.setTextAlignment(LEFT);//RIGHT
	temp = String(wunderground.getWindSpeed().toFloat(),1);
	ui.drawString(110, 300, temp);	// vitesse vent
	temp = String(wunderground.getWindGust().toFloat(),1);
	tft.setFont(&ArialRoundedMTBold_14);
	ui.drawString(200, 283, temp);	// vitesse vent	
	tft.setFont(&ArialRoundedMTBold_14);
	ui.drawString(200, 300, "km/h");	// vitesse vent	
}
//----------------------------------------------------------------------------------------------//
void draw_ecran2(){// ecran complement meteo pression point rosée
	String temp;
	tft.fillScreen(ILI9341_BLACK);
	drawTime();
	ui.drawBmp("/baro" + extBmp, 20, 70);									// icone barometre
	tft.setFont(&ArialRoundedMTBold_36);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(LEFT);//RIGHT
	if(config.UseMaMeteo){
		temp = String(maMeteo.pression,0);
	}
	else{
		temp = String(wunderground.getPressure().toFloat(),0);
	}
	ui.drawString(110, 125, temp);	// pression atmo
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(LEFT);	
	ui.drawString(200, 125, " mb");
	
	ui.drawBmp("/hygro" + extBmp, 20, 160);									// icone Hygrometre
	tft.setFont(&ArialRoundedMTBold_36);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(LEFT);//RIGHT
	if(config.UseMaMeteo){
		temp = String(maMeteo.humid,0);
	}
	else{
		temp = String(wunderground.getHumidity().toFloat(),0);
	}	
	ui.drawString(110, 212, temp);			// rh
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(LEFT);	
	ui.drawString(200, 212, " %");
	
	ui.drawBmp("/ptr" + extBmp, 20, 255);									// icone Point rosée
	tft.setFont(&ArialRoundedMTBold_36);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(LEFT);//RIGHT	
	if(config.UseMaMeteo){
		temp = String(dewPointFast(maMeteo.temp, maMeteo.humid),1);
	}
	else{
		temp = String(wunderground.getDewPoint().toFloat(),1);
	}	
	ui.drawString(110, 300, temp);			// ptr
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(LEFT);	
	ui.drawString(200, 300, " C");

}
//----------------------------------------------------------------------------------------------//
void draw_ecran3(){// ecran systeme station
	tft.fillScreen(ILI9341_BLACK);
	drawTime();
	String temp;
	// Wifi SSID abcdefgh
	// Wifi RSSI -100 dBm
	// Batterie 
	// versoft
 	
	ui.drawBmp("/wifi" + extBmp, 20, 70);									// icone Wifi
	tft.setFont(&ArialRoundedMTBold_14);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(RIGHT);
	temp = maMeteo.ssid;								// SSID de la station
	ui.drawString(239, 90, temp);
	temp = String(maMeteo.rssi,0);			// rssi de la station
	temp += F(" dBm");
	ui.drawString(239, 108, temp);
	
	ui.drawBmp("/batt" + extBmp, 20, 160);									// icone Batterie
	tft.setFont(&ArialRoundedMTBold_36);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(LEFT);
  temp = String(Battpct(maMeteo.vbatt));
	ui.drawString(110, 212, temp);
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(RIGHT);
	temp = String(maMeteo.vbatt/1000,2);
	temp += F(" V ");
	ui.drawString(239, 180, temp);	
	temp = F("%      ");	
	ui.drawString(239, 212, temp);
	
	temp = "soft ver : ";															// version soft station	
	temp += String(maMeteo.versoft);
	ui.setTextAlignment(LEFT);
	ui.drawString(10, 250, temp);
	// //debug.print("ver station = "), //debug.println(maMeteo.versoft);
	ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);	
	temp = "Parametres Station Meteo";
	ui.drawString(20, 300, temp);
}
//----------------------------------------------------------------------------------------------//
void draw_ecran4(){// ecran ecran 
	tft.fillScreen(ILI9341_BLACK);
	drawTime();
	String temp;
	// Wifi SSID abcdefgh
	// Wifi RSSI -100 dBm
	// Wifi Ip
	// Batterie 
	// versoft
	// ville 	
	ui.drawBmp("/wifi" + extBmp, 20, 70);									// icone Wifi
	tft.setFont(&ArialRoundedMTBold_14);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(RIGHT);
  temp = WiFi.SSID();							// SSID ecran local
	ui.drawString(239, 90, temp);
	temp = String(WiFi.RSSI());			// rssi ecran local
	temp += F(" dBm");
	ui.drawString(239, 108, temp);	
	temp = WiFi.localIP().toString();		// ip ecran local
	ui.drawString(239, 125, temp);
	
	ui.drawBmp("/batt" + extBmp, 20, 160);									// icone Batterie
	tft.setFont(&ArialRoundedMTBold_36);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.setTextAlignment(LEFT);
  	temp = String(TensionBatterie/1000,2);
	ui.drawString(110, 212, temp);
	
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(RIGHT);
	// temp = String(TensionBatterie/1000,2);
	// temp += F(" V ");
	// ui.drawString(239, 180, temp);	
	temp = F("V     ");	
	ui.drawString(239, 212, temp);
	
	temp  = soft.substring(0,16);														// version soft ecran						
	temp += String(ver);
	ui.setTextAlignment(LEFT);
	ui.drawString(10, 250, temp);
	ui.setTextAlignment(CENTER);
	ui.drawString(117, 275, ville[1][config.city]);
  ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);	
	temp = "Parametres Ecran Meteo";
	ui.drawString(117, 300, temp);	
}
//----------------------------------------------------------------------------------------------//
void draw_ecran41(byte zzone){// partie basse ecran 4
	// zzone = 1 gauche, 2 centre, 3 droite
	// on ecrit au centre la liste des villes dispo, actuelle couleur bleue
	// de part  et d'autre une fleche montante descendante
	// on selectionne en touchant le centre et sortie
	
	static byte numlign = 0;
	
	if (zzone == 2){// enregistrement nouvelle ville de reference
		
		if(numlign + 1 <= nbrVille && numlign + 1 != config.city && numlign + 1 !=0){
			config.city = numlign + 1; // nouvelle ville
			// WUNDERGROUND_CITY = ville[0][config.city];
			// EcrireEEPROM(0);
			EEPROM.put(0,config);
			EEPROM.commit();
			delay(100);
			EEPROM.get(0,config);
			delay(100);
			Serial.print(ville[1][config.city]);
			numlign = 0;
			updateData();
			draw_ecran0();
			goto fin;
		}
	}
	if (zzone == 1){
		if(numlign > 0) numlign --;
	}
	if (zzone == 3){
		if(numlign < nbrVille - 1) numlign ++;
	}
	Serial.print(F("zzone :")),Serial.println(zzone);
	Serial.print(F("numligne :")),Serial.println(numlign);
	
	tft.fillRect(0, 234, tft.width(), 86,ILI9341_BLACK); // efface existant
	ui.drawBmp("/s" + extBmp , 20 , 245);// icone vent sud 
	ui.drawBmp("/n" + extBmp , 164, 245);// icone vent nord		
	
	ui.setTextAlignment(CENTER);
	tft.setFont(&ArialRoundedMTBold_14);
	
	for(int i = 0;i < 3;i++){
		if(numlign + i <= nbrVille){
			if(numlign + i == config.city){
				ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
			}
			else{
				ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
			}
			ui.drawString(117, 250 + (i*25), ville[1][numlign+i]);	
		}
	}
	tft.drawFastHLine(78 , 260, 84, ILI9341_WHITE);
	tft.drawFastHLine(78 , 280, 84, ILI9341_WHITE);
	tft.drawFastVLine(78 , 260, 20, ILI9341_WHITE);
	tft.drawFastVLine(162, 260, 20, ILI9341_WHITE);
	fin:
	delay(10);
}
//----------------------------------------------------------------------------------------------//
int Battpct(long vbat){
	int EtatBat = 0;
	if (vbat > 4150) {
		EtatBat = 100;
	}
	else if (vbat > 4100) {
		EtatBat = 90;
	}
	else if (vbat > 3970) {
		EtatBat = 80;
	}
	else if (vbat > 3920) {
		EtatBat = 70;
	}
	else if (vbat > 3870) {
		EtatBat = 60;
	}
	else if (vbat > 3830) {
		EtatBat = 50;
	}
	else if (vbat > 3790) {
		EtatBat = 40;
	}
	else if (vbat > 3750) {
		EtatBat = 30;
	}
	else if (vbat > 3700) {
		EtatBat = 20;
	}
	else if (vbat > 3600) {
		EtatBat = 10;
	}
	else if (vbat > 3300) {
		EtatBat = 5;
	}
	else if (vbat <= 3000) {
		EtatBat = 0;
	}
	return EtatBat;
}
//----------------------------------------------------------------------------------------------//
double dewPointFast(double celsius, double humidity){
 double a = 17.271;
 double b = 237.7;
 double temp = (a * celsius) / (b + celsius) + log(humidity*0.01);
 double Td = (b * temp) / (a - temp);
 return Td;
}
//----------------------------------------------------------------------------------------------//
void GereEcran(){
	/* ecran decoupé en 4 zones horizontales et 3 verticales
			1		10		100
			2		20		200
			3		30		300	
			4		40		400
	*/

	switch (zone){
		case 1:
			break;
		case 10:
			break;
		case 100:
			break;
		case 2:
			if (ecran > 0){
				ecran --;
			}
			else{
				ecran = nbrecran;
			}
			draw_ecran(ecran);
			break;
		case 20:
			if(ecran == 0){
				zone = 200;
			}
			break;
		case 200:
			if (ecran < nbrecran){
				ecran ++;
			}
			else{
				ecran = 0;				
			}
			draw_ecran(ecran);
			break;
		case 3:
			if(ecran == 0){
				if(frcst > 0) {
					frcst --;
				}
				else{
					frcst = 2;
				}
				//debug.print(F("frcst =")),//debug.println(frcst);
				drawForecast(frcst);
			}
			break;
		case 30:
			if(ecran == 0){
				zone = 300;
			}
			break;
		case 300:
			if(ecran == 0){
				if(frcst < 2) {
					frcst ++;
				}
				else{
					frcst = 0;
				}
				//debug.print(F("frcst =")),//debug.println(frcst);
				drawForecast(frcst);
			}	
			break;
		case 4:
			// zone = 40;
			if (ecran == 4){
				draw_ecran41(1);	// selection ville
			}
			break;
		case 400:
			// zone = 40;
			if (ecran == 4){
				draw_ecran41(3);	// selection ville
			}
			break;
		case 40:
			if (ecran == 0){
				if (FlagAstronomy){ 
					drawVille();
					FlagAstronomy = false;
				}
				else{
					drawAstronomy();
					FlagAstronomy = true;
				}
			}
			if (ecran == 4){
				draw_ecran41(2);	// selection ville
			}
			break;
		
	}
}
//----------------------------------------------------------------------------------------------//
void draw_ecran(byte i){
	// i = ecran 
	switch(i){
		case 0:
		draw_ecran0();
		break;
	}
	switch(i){
		case 1:
		draw_ecran1();
		break;
	}
	switch(i){
		case 2:
		draw_ecran2();
		break;
	}
	switch(i){
		case 3:
		if(config.UseMaMeteo){
			draw_ecran3();
			break;
		}
		else{//suivant
			break;
		}		
	}
	switch(i){
		case 4:
		draw_ecran4();
		break;
	}
}
//----------------------------------------------------------------------------------------------//
void MajSoft() {
  //////////////////////////////cherche si mise à jour dispo et maj///////////////////////////////////////
  //debug.print(F("Vérification si une mise à jour est disponible ? "));
  
  String Fichier = "http://";
  Fichier += monSite;
  Fichier += "/bin/" ;
  Fichier += soft;
  Fichier += String (ver + 1); // cherche version actuelle + 1
  Fichier += ".bin";
  //debug.println(Fichier);
  t_httpUpdate_return ret = ESPhttpUpdate.update(Fichier);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      //Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      //debug.println(F("Pas de mise à jour disponible !"));
      // messageMQTT = "Pas de Maj Soft";
      break;
    case HTTP_UPDATE_NO_UPDATES:
      //debug.println(F("HTTP_UPDATE_NO_UPDATES"));
      break;
    case HTTP_UPDATE_OK:
      //debug.println(F("HTTP_UPDATE_OK"));
      break;
  }
}
//---------------------------------------------------------------------------
/* void EcrireEEPROM(byte adr){
	// while (!eeprom_is_ready());
	int longueur = EEPROM_writeAnything(adr, config);	// ecriture des valeurs par defaut
	delay(10);
	EEPROM_readAnything(0, config);
	Serial.print(ville[1][config.city]);
	Serial.print(F("longEEPROM=")),Serial.println(longueur);
} */
//--------------------------------------------------------------------------------//
String getTime(time_t *timestamp) {
  // struct tm *timeInfo = gmtime(timestamp);
	struct tm *timeInfo = localtime(timestamp);
  
  char buf[6];
  sprintf(buf, "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
  return String(buf);
}