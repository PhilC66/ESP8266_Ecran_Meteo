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
See more at http://blog.squix.ch , https://thingpulse.com
*/

/* 320x240 */
/* Pin out de l'ESP8266
0   TFT CS
1   TX
2   SD card CS
3   RX
4   Back Light
5   SD Card detect ou IRQ touch
12  MISO
13  MOSI
14  SCK
15  TFT DC
16  RT CS Touch Screen
*/

/* recherche mise a jour soft à 03hmm sur site perso, à mm aléatoire */

/* carte HUZZAH esp8266	4M(3M SPIFFS)
422156 40%, 43840 53%

----------------- ATTENTION -----------------
		ne fonctionne pas avec les mise a jour récente
		version ci-dessous seulement
		carte     esp8266 community V 2.3.0
		librairie esp8266 wifimanager by tzapu V 0.12.0
		
----------------- ATTENTION -----------------		
----------------------to do----------------------------------
securiser extraction data Mameteo bug si message erreur apres les données
il ne faut pas prendre dernier " ou dernier :
// il faut progresser dans les datas jusqu'a la fin
ou utiliser JSON decode

trouver caracteres >127

-------------------------------------------------------------*/
/*
V100 18/11/2018 Migration Wunderground vers Weathermap
*/

#include <Arduino.h>
#include <credentials_home.h>       // mes données personnelles
#include <Adafruit_GFX.h>           // Core graphics library
#include <Adafruit_ILI9341.h>       // Hardware-specific library
#include <SPI.h>
#include <Wire.h>                   // required even though we do not use I2C 
#include "Adafruit_STMPE610.h"      // touch screen
#include "GfxUi.h"                  // Additional UI functions
#include "ArialRoundedMTBold_14.h"  // Fonts created by http://oleddisplay.squix.ch/
#include "ArialRoundedMTBold_36.h"
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>            // Helps with connecting to Wifi
#include <ESP8266httpUpdate.h>      // Update Over The Air
#include "settings.h"               // check settings.h for adapting to your needs
#include <JsonListener.h>
#include <OpenWeatherMapCurrent.h>  // données météo
#include <OpenWeatherMapForecast.h> // données météo
#include <Astronomy.h>              // calcul phase lune
#include <EEPROM.h>                 // variable en EEPROM

#define HOSTNAME "ESP_EcranMeteo"  // HOSTNAME for OTA update
struct config_t                    // configuration sauvée en EEPROM
{
	byte 		magic;                   // num magique
	byte 		city;                    // numero ville
	boolean UseMaMeteo;              // utilise data mameteo ou openweathermap si false
} config;

const String soft = "ESP8266_E_Meteo.ino.adafruit"; 	// nom du soft
const int 	 ver  = 103;

const byte nbrVille	= 5;
String ville[3][nbrVille+1] ={
	{"          ","3014084" ,"3031848","3020035","2993728"  ,"2987914"  },
	{"          ","Hagetmau","Bompas" ,"Epinal" ,"Mirecourt","Perpignan"},
	{"0"         ,"Labastide-Chalosse"       ,"0"      ,"0"      ,"0"        ,"0"}
};// 0 Weathermap ID , 1 Nom Ville, 2 0 ou nom Station perso liée

float  TensionBatterie; // batterie de l'ecran
String extBmp = ".bmp";

struct t {
float   temp;
float   tempmin;
float   tempmax;
float   humid;
float   pression;
float   rain1h;
float   rain24h;
int     derpluie;     // nbr de jour depuis derniere pluie
float   der24h;       // pluie ce dernier jour de pluie
float   pluie7j;
float   pluie30j;
float   pluiemax;
bool    last;         // 1 si >1 heure depuis dernier enregistrement dans la table
float   vbatt;        // batterie de la station
float   rssi;
String  ssid;
int     versoft;
String  derjour;
} maMeteo;            //	données ma station meteo

byte API_KEY_Nbr;     // selection API_KEY selon ville

struct tj {float tempmin; float tempmax;} tempj ; // memorisation temp min/max du jour
String FileDataJour = "/FileDataJour.txt";        // Fichier en SPIFF data du jour

byte ecran          = 0; // ecran actif
int  zone           = 0; // zone de l'ecran
byte frcst          = 0; // compteur forecast affiché 
byte nbrecran       = 4; // nombre ecran existant
byte MinMajSoft     = 0; // minute de verification mise à jour soft
	
long lastDownloadUpdate = millis();
long lastDrew           = millis();
long lastRotation       = millis();
time_t dstOffset        = 0;
uint8_t moonAgeImage    = 0;
uint8_t moonAge         = 0;

#define Ip_Analogique 0					// entree analogique mesure tension
#define Op_BackLight  4					// sortie commande Backlight PWM
// limit de la zone de touché
#define TS_MINX 150
#define TS_MINY 130
#define TS_MAXX 3800
#define TS_MAXY 4000

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
GfxUi ui = GfxUi(&tft);

Adafruit_STMPE610 spitouch = Adafruit_STMPE610(STMPE_CS);

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
void drawAstronomy();
void drawCurrentWeatherDetail();
void drawLabelValue(uint8_t line, String label, String value);
String getTime(time_t *timestamp);
const char* getMeteoconIcon(String iconText);
boolean JourNuit();
void MesureBatterie();
void GereEcran();
void draw_ecran(byte i);
void MajSoft();

WiFiClient client;
//--------------------------------------------------------------------------------//  
void setup() {
	pinMode(Op_BackLight, OUTPUT);
	analogWrite(Op_BackLight,1023);// Backlight ON
	Serial.begin(115200);

	if (! spitouch.begin()) {
		Serial.println(F("STMPE not found?"));
	}
	// lecture EEPROM
	byte defaultmagic = 121;
	EEPROM.begin(512);
	EEPROM.get(0,config);
	delay(100);
	if(config.magic != defaultmagic){
		config.magic      = defaultmagic;
		config.city       = 1;
		config.UseMaMeteo = false;
		EEPROM.put(0,config);
		EEPROM.commit();
		delay(100);
		Serial.print(F("Nouvelle ville : ")),Serial.println(ville[1][config.city]);
	}
	MinMajSoft = random(0,10);	// determine la minute mise a jour pour limiter collision

	if(config.city == 1){		// selection API key en fonction de la ville
		API_KEY_Nbr = 0;
	}
	else if(config.city == 2 || config.city == 5){
		API_KEY_Nbr = 1;
	}
	else if(config.city == 3 || config.city == 4){
		API_KEY_Nbr = 2;
	}

	tft.begin();
	tft.fillScreen(ILI9341_BLACK);
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
	ui.setTextAlignment(CENTER);
	ui.drawString(120, 160, "Connexion WiFi");

	/* WiFiManager */
	WiFiManager wifiManager;
	/* Uncomment for testing wifi manager */
	// wifiManager.resetSettings();
	wifiManager.setAPCallback(configModeCallback);

	/* or use this for auto generated name ESP + ChipID */
	if(!wifiManager.autoConnect()){
		Serial.println(F("failed to connect and hit timeout"));
		ui.drawString(120, 180, "Impossible de se connecter");
		delay(1000);
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(1000);
	} 

	/* Manual Wifi */
	//WiFi.begin(mySSID, myPASSWORD);
	// while(WiFi.status() != WL_CONNECTED){
		// Serial.print(".");
		// delay(1000);
	// }
	// Serial.println();
	//WiFi.printDiag(Serial);

	Serial.print(F("connecté : ")), Serial.println(WiFi.SSID());
	ui.drawString(120, 180, "Connecte");
	String temp = String(WiFi.SSID());
	ui.drawString(120, 200, temp);
	delay(1000);

	// OTA Setup
	String hostname(HOSTNAME);
	// hostname += String(ESP.getChipId(), HEX);
	WiFi.hostname(hostname);
	ArduinoOTA.setHostname((const char *)hostname.c_str());
	ArduinoOTA.begin();
	SPIFFS.begin();

	/* Uncomment if you want to update all internet resources */
	//SPIFFS.format();
	loadFileSpiffs(); // verification et chargement fichiers icones

	updateTime();
	updateForecast();
	updateData();

		/* SPIFFS.remove(FileDataJour);
		File f = SPIFFS.open(FileDataJour, "w");
		float s1 = -10.5;
		float s2 = +30.2;
		f.println(s1);
		f.println(s2);
		f.close();  //Close file */

	if(SPIFFS.exists(FileDataJour)){ // Lecture fichier data jour
		File f = SPIFFS.open(FileDataJour, "r");  
		// Serial.print("Reading Data from File:"),Serial.println(f.size());
		for(int i = 0;i < 2;i++){ //Read
			String s = f.readStringUntil('\n');
			Serial.print(i),Serial.print(" "),Serial.println(s);
			if(i==0)tempj.tempmin = s.toFloat();
			if(i==1)tempj.tempmax = s.toFloat();
		}
		f.close();
		// Serial.println("File Closed");
	}
	else{
		Serial.println(F("Creating Data File:"));	
		tempj.tempmin = currentWeather.temp;
		tempj.tempmax = currentWeather.temp;
		Recordtempj();
	}
	updateMinMax();
	Serial.print(F("Temp min =")),Serial.println(tempj.tempmin);
	Serial.print(F("Temp max =")),Serial.println(tempj.tempmax);


	MesureBatterie();
	MajSoft();	// verification si maj soft disponible
	draw_ecran0();
}
//--------------------------------------------------------------------------------//
void loop() {
	static byte cpt = 0;	// compte nbr passage tous les 4 passages update previsions
	static byte cptlancemeteo = 0;
	static boolean flaglancemeteo = true;
	static bool majdatajour = false;
	// static byte n   = 0;
	char *dstAbbrev;
	time_t now = dstAdjusted.time(&dstAbbrev);
	struct tm * timeinfo = localtime (&now);

	if (!spitouch.bufferEmpty()){       // si ecran touché
		analogWrite(Op_BackLight,1023);   // Backlight ON
		TS_Point p = spitouch.getPoint(); // recupere les coordonnées
		p.x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
		p.y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());
		zone = 0;	
		int x = tft.width() - p.x;
		int y = p.y;
		
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
		
		GereEcran();
		
		//vider buffer
		while(!spitouch.bufferEmpty()){
			TS_Point p = spitouch.getPoint();
		}
	}
	if(ecran == 0 && millis() - lastRotation > 5000){// rotation zone forecast
		lastRotation = millis();
		zone = 300;
		GereEcran();
	}
	if(millis() - lastDrew > 10000 && timeinfo->tm_sec == 0){
		drawTime();
		lastDrew = millis();
		Serial.print(timeinfo->tm_hour),Serial.print(":"),Serial.print(timeinfo->tm_min),Serial.print(":"),Serial.println(timeinfo->tm_sec);
		if(timeinfo-> tm_hour == 3 && timeinfo-> tm_min == MinMajSoft){	
			// Majsoft entre 03h00 et 03h10
			Serial.println(F("Mise a jour Soft"));
			MajSoft();
			updateTime();
		}
		if(timeinfo-> tm_hour == 0 && timeinfo-> tm_min == 0 && !majdatajour){//&& String(timeinfo-> tm_sec) == "0"
			// Mise à jour des data du jour
			tempj.tempmin = currentWeather.temp;
			tempj.tempmax = currentWeather.temp;
			printf("%s %02d:%02d:%02d\n","record data jour",timeinfo->tm_hour, timeinfo->tm_min,timeinfo->tm_sec);
			Recordtempj();
			draw_ecran0();
			majdatajour = true;
		}
		if(timeinfo-> tm_hour == 0 && timeinfo-> tm_min == 1 ) majdatajour = false;
		
		MesureBatterie();	
		if(JourNuit()){
			Serial.println(F("Jour"));
			analogWrite(Op_BackLight,1023);    // Backlight ON 1023
		}
		else{
			Serial.println(F("Nuit"));
			analogWrite(Op_BackLight,50);      // Backlight OFF 50
		}
		if (millis() - lastDownloadUpdate > 1000 * UPDATE_INTERVAL_SECS) {
			updateData();
			updateMinMax();
			flaglancemeteo = true;
			if(cpt == 3){ // mise à jour Forecast seulement tous les 4 passages(1H)
				updateForecast();
				cpt = 0;
			}else{
				cpt ++;
			}
			ecran = 0;
			draw_ecran0();
			lastDownloadUpdate = millis();
		}
		if(flaglancemeteo && !config.UseMaMeteo && ville[2][config.city] != "0"){// nouvelle tentative lecture Mameteo
			// tentative de mise à jour mameteo en cas d'echec
			cptlancemeteo ++;
			lanceMameteo();
			if(cptlancemeteo > 4){ // arret apres 5 tentatives
				cptlancemeteo = 0;
				flaglancemeteo = false; // reporté à la prochaine boucle mise à jour 
			}
			if(flaglancemeteo){
				draw_ecran0();
			}
		}
	}
		
	ArduinoOTA.handle();	// Handle OTA update requests
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
	Serial.print(F("Tension Batterie = ")),Serial.println(TensionBatterie);
}
//--------------------------------------------------------------------------------//
void configModeCallback (WiFiManager *myWiFiManager) {
	/* Called if WiFi has not been configured yet */
	ui.setTextAlignment(CENTER);
	tft.setFont(&ArialRoundedMTBold_14);
	tft.setTextColor(ILI9341_CYAN);
	ui.drawString(120, 28, "Gestion Wifi"); // "Wifi Manager"
	ui.drawString(120, 42, "Se connecter a "); // "Please connect to AP"
	tft.setTextColor(ILI9341_WHITE);
	ui.drawString(120, 56, myWiFiManager->getConfigPortalSSID());
	tft.setTextColor(ILI9341_CYAN);
	ui.drawString(120, 70, "Acceder a la page de configuration"); // "To setup Wifi Configuration"
	ui.drawString(120, 84, "192.168.4.1");
}
//--------------------------------------------------------------------------------//
void draw_ecran0(){ // ecran principal
	tft.fillScreen(ILI9341_BLACK);
	drawTime();
	drawCurrentWeather();
	drawForecast(0);
	drawAstronomy();
}
//--------------------------------------------------------------------------------//
void lanceMameteo(){
	byte cpt = 0;
	do{
		Mameteo();
		if(config.UseMaMeteo){	// si lecture ok on sort
			// Serial.print(F("nbr lecture Mameteo:")),Serial.println(cpt);
			cpt = 5;
		}
		cpt++;					// si non on boucles
		delay(500);
	}while (cpt < 5);
}
//--------------------------------------------------------------------------------//
void updateData() { // Update the internet based information and update screen

	tft.fillScreen(ILI9341_BLACK);
	tft.setFont(&ArialRoundedMTBold_14);

	if(ville[2][config.city] != "0"){
		drawProgress(40, "Maj MaMeteo...");
		lanceMameteo();
	}

	drawProgress(50, "Maj actuelle...");
	OpenWeatherMapCurrent *currentWeatherClient = new OpenWeatherMapCurrent();
	currentWeatherClient->setMetric(IS_METRIC);
	currentWeatherClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
	currentWeatherClient->updateCurrentById(&currentWeather, Openweathermap_key[API_KEY_Nbr],ville[0][config.city]);
	delete currentWeatherClient;
	currentWeatherClient = nullptr;

	drawProgress(80, "Maj astronomie...");
	Astronomy *astronomy = new Astronomy();
	moonData = astronomy->calculateMoonData(time(nullptr));
	float lunarMonth = 29.53;
	moonAge = moonData.phase <= 4 ? lunarMonth * moonData.illumination / 2 : lunarMonth - moonData.illumination * lunarMonth / 2;
	moonAgeImage = int(24 * moonAge/lunarMonth);
	if(moonAgeImage == 0)moonAgeImage = 23;

	delete astronomy;
	astronomy = nullptr;
	drawProgress(100, "Fin...");
	delay(500);
}
//--------------------------------------------------------------------------------//
void drawProgress(uint8_t percentage, String text) { // Progress bar helper
  ui.setTextAlignment(CENTER);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.fillRect(0, 140, 240, 45, ILI9341_BLACK);
  ui.drawString(120, 160, text);
  ui.drawProgressBar(10, 165, 240 - 20, 15, percentage, ILI9341_WHITE, ILI9341_BLUE);
}
//--------------------------------------------------------------------------------//
void drawTime() { // draws the clock
	char time_str[11];
	char *dstAbbrev;
	time_t now = dstAdjusted.time(&dstAbbrev);
	struct tm * timeinfo = localtime (&now);

	tft.fillRect(0, 0, tft.width(), 57,ILI9341_BLACK); // efface existant
	ui.setTextAlignment(CENTER);
	ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
	tft.setFont(&ArialRoundedMTBold_14);
	String madate = WDAY_NAMES[timeinfo->tm_wday] + " " + String(timeinfo->tm_mday) + " "+ MONTH_NAMES[timeinfo->tm_mon] + " " + String(1900 + timeinfo->tm_year);
	// Serial.print(F("Date du jour : ")),Serial.println(madate);
	ui.drawString(120, 20, madate);

	tft.setFont(&ArialRoundedMTBold_36);
	sprintf(time_str, "%02d:%02d\n",timeinfo->tm_hour, timeinfo->tm_min);
	ui.drawString(120, 56, time_str);
	Serial.println(time_str);
	ui.setTextAlignment(RIGHT);
	ui.setTextColor(ILI9341_LIGHTGREY, ILI9341_BLACK);
	tft.setFont(&ArialRoundedMTBold_14);
	sprintf(time_str, "%s", dstAbbrev);
	ui.drawString(238, 45, time_str);
	// Serial.println(time_str);
}
//--------------------------------------------------------------------------------//
void drawCurrentWeather() { // draws current weather information

	// Weather Icon
	String weatherIcon = getMeteoconIcon(currentWeather.icon);
	ui.drawBmp("/" + weatherIcon + extBmp, 0, 55);
	// Serial.print(F("Icone = ")),Serial.println(weatherIcon);

	// Weather Ville
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextColor(ILI9341_LIGHTGREY, ILI9341_BLACK);
	ui.setTextAlignment(RIGHT);
	if(config.UseMaMeteo && ville[2][config.city] != "0"){
		ui.drawString(239, 85, ville[2][config.city]);
	}
	else{
		ui.drawString(239, 85, ville[1][config.city]);// 239,90
	}

	// Weather temperature  
	if(ville[2][config.city] == "0"){ // ville sans station meteo perso
		tft.setFont(&ArialRoundedMTBold_36);
		ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		ui.setTextAlignment(LEFT);//RIGHT
		ui.drawString(110, 120, String(currentWeather.temp, 1));// + (IS_METRIC ? "°C" : "°F"));//220 Temperature Wunderground
		tft.setFont(&ArialRoundedMTBold_14);
		ui.setTextAlignment(RIGHT);
		// tempj.tempmin = -10;
		if(tempj.tempmax > 30) {													//tempmax
			ui.setTextColor(ILI9341_MAGENTA, ILI9341_BLACK);//mettre en rouge sinon cyan
		}
		else{
			ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		}
		ui.setTextAlignment(RIGHT);
		ui.drawString(239, 100, String(tempj.tempmax,1));//108

		if(tempj.tempmin < 0){													//tempmin
			ui.setTextColor(ILI9341_BLUE, ILI9341_BLACK);	
		}
		else{
			ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		}	
		ui.drawString(239, 122, String(tempj.tempmin,1));//239,125
	}
	else if(config.UseMaMeteo && ville[2][config.city] != "0"){ // ville avec station meteo perso
		tft.setFont(&ArialRoundedMTBold_36);
		ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		ui.setTextAlignment(LEFT);//RIGHT
		ui.drawString(110, 120, String(maMeteo.temp,1));//110,125
		
		// maMeteo.tempmax = 45.2;
		if(maMeteo.tempmax > 30) {													//tempmax
			ui.setTextColor(ILI9341_MAGENTA, ILI9341_BLACK);//mettre en rouge sinon cyan
		}
		else{
			ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		}
		tft.setFont(&ArialRoundedMTBold_14);
		ui.setTextAlignment(RIGHT);
		ui.drawString(239, 100, String(maMeteo.tempmax,1));//108

		// maMeteo.tempmin = -25.5;	
		if(maMeteo.tempmin < 0){													//tempmin
				ui.setTextColor(ILI9341_BLUE, ILI9341_BLACK);	
		}
		else{
			ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		}	
		ui.drawString(239, 122, String(maMeteo.tempmin,1));
	}else if(!config.UseMaMeteo && ville[2][config.city] != "0"){
		tft.setFont(&ArialRoundedMTBold_36);
		ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
		ui.setTextAlignment(LEFT);//RIGHT
		ui.drawString(110, 120, String(currentWeather.temp, 1));// + (IS_METRIC ? "°C" : "°F"));
		tft.setFont(&ArialRoundedMTBold_14);
		ui.setTextAlignment(RIGHT);
		ui.setTextColor(ILI9341_MAGENTA, ILI9341_BLACK);
		ui.drawString(239, 120, "#");
	}

	// Weather Text
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
	ui.setTextAlignment(RIGHT);
	ui.drawString(239, 137, RemplaceCharSpec(currentWeather.description));//239,140
	// drawSeparator(135);

	//------------------------ test --------------------------------
/* 		String bidon[12]={"01d","01n","02d","02n","03d","04d","04n","09d","10d","11d","13d","50d"};
		String weatherIcon0;
		for(int i = 0; i<12;i++){
			weatherIcon0 = getMeteoconIcon(bidon[i]);
			ui.drawBmp("/" + weatherIcon0 + extBmp, 0, 55);
			ui.drawString(239, 135, weatherIcon0);//239,140
			Serial.print(F("Icone = ")),Serial.println(weatherIcon0);
			delay(1000);
		} */
  //------------------------ test --------------------------------
}
//--------------------------------------------------------------------------------//
void drawForecast(byte seq) { // draws the three forecast columns	

	byte j = 0;
	if(seq == 0) j = 0;
	if(seq == 1) j = 3;
	if(seq == 2) j = 6;
	tft.fillRect(0, 140, tft.width(), 100,ILI9341_BLACK); // 0,153,width,80 efface existant

	drawForecastDetail(40  , 175, j);	//10 165
	drawForecastDetail(120 , 175, j+1);//95 165
	drawForecastDetail(200 , 175, j+2);//180 165
}
//--------------------------------------------------------------------------------//
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {

	// forecast columns
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(CENTER);//CENTER
	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
	time_t time = forecasts[dayIndex].observationTime + dstOffset;
	struct tm * timeinfo = localtime (&time);

	// Icone
	String weatherIcon = getMeteoconIcon(forecasts[dayIndex].icon);
	ui.drawBmp("/mini/" + weatherIcon + extBmp, x-30, y + 5);// x,y+10 y+15
	// Serial.print(F("miniIcone = ")),Serial.println(weatherIcon);

	// Jour heure	
	String prevheure;
	if(timeinfo->tm_hour > 11){		
		prevheure = String(timeinfo->tm_hour - 1);
	}
	else if(timeinfo->tm_hour < 10){
		prevheure = "0";
		prevheure +=  String(timeinfo->tm_hour - 1);	
	}
	else{
		prevheure = "23";
	}

	ui.drawString(x + 0, y - 15, WDAY_NAMES[timeinfo->tm_wday].substring(0,2) + " " + prevheure + ":00");//x+25 y-15

	// Temperature
	ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
	ui.drawString(x + 0, y, String(forecasts[dayIndex].temp, 1) + (IS_METRIC ? " C" : "°F"));

	// Pluie
	ui.setTextColor(ILI9341_BLUE, ILI9341_BLACK);
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(CENTER);
	// Serial.print(F("forecast pluie = ")),Serial.println(forecasts[dayIndex].rain);
	if(forecasts[dayIndex].rain > 0){
		if(forecasts[dayIndex].rain < 1){
			ui.drawString(x + 0, y + 65, String("< 1") + (IS_METRIC ? "mm" : "in"));
		}
		else{
			ui.drawString(x + 0, y + 65, String(forecasts[dayIndex].rain, 2) + (IS_METRIC ? "mm" : "in"));
		}
	}
}
//--------------------------------------------------------------------------------//
void drawAstronomy() {
	/* draw moonphase and sunrise/set and moonrise/set */
	tft.fillRect(0, 244, tft.width(), 86,ILI9341_BLACK); // efface existant 0,234

	time_t timesunrise = currentWeather.sunrise + dstOffset;
	time_t timesunset  = currentWeather.sunset  + dstOffset;

	Serial.print(F("Moon Age :")), Serial.print(moonAge), Serial.print(F(", moonImage :")),Serial.print(moonAgeImage);
	Serial.print(F(", phase :")),Serial.println(moonData.phase);

	ui.drawBmp("/moon" + String(moonAgeImage) + extBmp, 120 - 30, 255);

	// ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
	tft.setFont(&ArialRoundedMTBold_14);  
	ui.setTextAlignment(LEFT);
	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
	ui.drawString(20, 270, F("Soleil"));
	ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
	ui.drawString(20, 285, getTime(&timesunrise));
	ui.drawString(20, 300, getTime(&timesunset));

	ui.setTextAlignment(RIGHT);
	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
	ui.drawString(220, 270, F("Lune"));
	ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
	ui.drawString(220, 285, String(24 * moonAge / 30) + "d");
	ui.drawString(220, 300, String(moonData.illumination * 100, 0) + "%");
}

//--------------------------------------------------------------------------------//
/* void drawSeparator(uint16_t y) {
	// if you want separators, uncomment the tft-line
	
  // tft.drawFastHLine(10, y, 240 - 2 * 10, 0x4228);
	tft.drawFastHLine(0, y, 200, ILI9341_RED);
} */

//--------------------------------------------------------------------------------//
String RemplaceCharSpec(String texte){
	/* Remplacer les caracteres >127
	130,136-138 	par e 101
	131-134 			par a 97
	135 					par c 99
	si é caracteres recus 195 + 169
	*/
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
	return textec;
	// Serial.print(F("texte converti =")), Serial.println(texte);
}
//---------------------------------------------------------------------------
boolean JourNuit(){ 
	/* determine si jour ou nuit
	Jour = true, Nuit=false */
	
	char *dstAbbrev;
	time_t now = dstAdjusted.time(&dstAbbrev);
	struct tm * timeinfo = localtime (&now);
	time_t timesunrise = currentWeather.sunrise + dstOffset;
	time_t timesunset  = currentWeather.sunset  + dstOffset;
	
	timesunrise -= 3600;	// marge 1 heure
	timesunset  += 7200;	// marge 2 heures
	if(timesunrise > 21600) timesunrise = 21600; // force jour à 6H00
	if(timesunset  < 79200) timesunset  = 79200; // force nuit à 22H00
	
	if(timesunset > timesunrise){
		if((now > timesunset && now > timesunrise)
		 ||(now < timesunset && now < timesunrise)){
			return false;	// Nuit
		}
		else{
			return true;	// Jour
		}
	}
	else{
		if(now > timesunset && now < timesunrise){
			return false;	// Nuit
		}
		else{
			return true;	// Jour
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
		delay(1000);	//	V17 500 necessaire avec box Guillaume
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
  /* convertir string en float ou int et copie dans variables */
	float varfloat = var.toFloat() ; 
	if (j == 1)  maMeteo.temp     = varfloat;
	if (j == 2)  maMeteo.tempmin  = varfloat;
	if (j == 3)  maMeteo.tempmax  = varfloat;
	if (j == 4)  maMeteo.humid    = varfloat;
	if (j == 5)  maMeteo.pression = varfloat;
	if (j == 6)  maMeteo.rain1h   = varfloat;
	if (j == 7)  maMeteo.rain24h  = varfloat;
	if (j == 8)  maMeteo.derpluie = var.toInt();
	if (j == 9)  maMeteo.pluie7j  = varfloat;
	if (j == 10) maMeteo.pluie30j = varfloat;
	if (j == 11) maMeteo.pluiemax = varfloat;
	if (j == 12) maMeteo.der24h   = varfloat;
	if (j == 13) maMeteo.last     = var.toInt();
	if (j == 14) maMeteo.vbatt    = varfloat;
	if (j == 15) maMeteo.rssi     = varfloat;
	if (j == 16) maMeteo.ssid     = var;
	if (j == 17) maMeteo.versoft  = var.toInt();
	
	// Serial.print(F("var=")), Serial.print(j), Serial.print(";"),Serial.print(var), Serial.print(";"), Serial.println(varfloat);
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
	if(config.UseMaMeteo && ville[2][config.city] != "0" ){ // pas d'info pluie avec API Openweathermap
		pluie1h  = maMeteo.rain1h;
		pluie24h = maMeteo.rain24h;
		
		if (pluie1h > 0){		
			ui.drawBmp("/pluie"  + extBmp, x, y);// icone pluie	faible	
		}
		else if(pluie1h > 2){	//	V18
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
	else{
		String degreeSign = "°F";
		if (IS_METRIC) {degreeSign = "°C";}
		tft.setFont(&ArialRoundedMTBold_14);
		ui.setTextAlignment(CENTER);
		ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
		ui.drawString(120, 90, "Current Conditions");

		drawLabelValue(0, "Temperature:", currentWeather.temp + degreeSign);
		drawLabelValue(1, "Wind Speed:" , String(currentWeather.windSpeed, 1) + (IS_METRIC ? "m/s" : "mph") );
		drawLabelValue(2, "Wind Dir:"   , String(currentWeather.windDeg, 1) + "°");
		drawLabelValue(3, "Humidity:"   , String(currentWeather.humidity) + "%");
		drawLabelValue(4, "Pressure:"   , String(currentWeather.pressure) + "hPa");
		drawLabelValue(5, "Clouds:"     , String(currentWeather.clouds) + "%");
		drawLabelValue(6, "Visibility:" , String(currentWeather.visibility) + "m");
	}
	x=20;
	y=255;
	// Zone Vent
	Serial.print(F("Vent :")),Serial.print(currentWeather.windSpeed),Serial.print(F(" m/s :")),Serial.println(currentWeather.windDeg);
	if(currentWeather.windDeg > 338 && currentWeather.windDeg < 24){
		ui.drawBmp("/n" + extBmp , x, y);// icone vent		
	}
	else if(currentWeather.windDeg > 23 && currentWeather.windDeg < 69){
		ui.drawBmp("/ne" + extBmp , x, y);
	}
	else if(currentWeather.windDeg > 68 && currentWeather.windDeg < 114){
		ui.drawBmp("/e" + extBmp , x, y);
	}
	else if(currentWeather.windDeg > 113 && currentWeather.windDeg < 159){
		ui.drawBmp("/se" + extBmp , x, y);
	}
	else if(currentWeather.windDeg > 158 && currentWeather.windDeg < 204){
		ui.drawBmp("/s" + extBmp , x, y);
	}
	else if(currentWeather.windDeg > 203 && currentWeather.windDeg < 249){
		ui.drawBmp("/so" + extBmp , x, y);
	}
	else if(currentWeather.windDeg > 248 && currentWeather.windDeg < 294){
		ui.drawBmp("/o" + extBmp , x, y);
	}
	else if(currentWeather.windDeg > 293 && currentWeather.windDeg < 339){
		ui.drawBmp("/no" + extBmp , x, y);
	}
	else{
		ui.drawBmp("/variable" + extBmp , x, y);
	}
	tft.setFont(&ArialRoundedMTBold_36);
	ui.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
	ui.setTextAlignment(LEFT);//RIGHT
	temp = String(currentWeather.windSpeed/1000*3600,1);
	ui.drawString(110, 300, temp);	// vitesse vent
	temp = String(currentWeather.windDeg,1);
	tft.setFont(&ArialRoundedMTBold_14);
	ui.drawString(200, 290, "km/h");	// vitesse vent
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
	if(config.UseMaMeteo && ville[2][config.city] != "0"){
		temp = String(maMeteo.pression,0);
	}
	else{
		temp = String(currentWeather.pressure);
	}
	ui.drawString(110, 125, temp);	// pression atmo
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(LEFT);	
	ui.drawString(200, 125, " hPa");

	ui.drawBmp("/hygro" + extBmp, 20, 160);									// icone Hygrometre
	tft.setFont(&ArialRoundedMTBold_36);
	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
	ui.setTextAlignment(LEFT);//RIGHT
	if(config.UseMaMeteo && ville[2][config.city] != "0"){
		temp = String(maMeteo.humid,0);
	}
	else{
		temp = String(currentWeather.humidity);
	}	
	ui.drawString(110, 212, temp);			// rh
	tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(LEFT);	
	ui.drawString(200, 212, " %");

	ui.drawBmp("/ptr" + extBmp, 20, 255);									// icone Point rosée
	tft.setFont(&ArialRoundedMTBold_36);
	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
	ui.setTextAlignment(LEFT);//RIGHT	
	if(config.UseMaMeteo && ville[2][config.city] != "0"){
		temp = String(dewPointFast(maMeteo.temp, maMeteo.humid),1);
	}
	else{
		temp = String(dewPointFast(currentWeather.temp, currentWeather.humidity),1);
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
	ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);	
	temp = "Parametres Station Meteo";
	ui.drawString(20, 300, temp);
}
//----------------------------------------------------------------------------------------------//
void draw_ecran4(){// ecran ecran 
	tft.fillScreen(ILI9341_BLACK);
	drawTime();
	String temp;

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

	/* ui.drawBmp("/batt" + extBmp, 20, 160);									// icone Batterie
	tft.setFont(&ArialRoundedMTBold_36);
	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
	ui.setTextAlignment(LEFT);
		temp = String(TensionBatterie/1000,2);
	ui.drawString(110, 212, temp); */

	/* tft.setFont(&ArialRoundedMTBold_14);
	ui.setTextAlignment(RIGHT);
	// temp = String(TensionBatterie/1000,2);
	// temp += F(" V ");
	// ui.drawString(239, 180, temp);	
	temp = F("V     ");	
	ui.drawString(239, 212, temp); */

	drawLabelValue(3, "Heap Mem :", String(ESP.getFreeHeap() / 1024)+" kb");
	drawLabelValue(4, "Chip ID :", String(ESP.getChipId()));
	drawLabelValue(5, "CPU Freq. : ", String(ESP.getCpuFreqMHz()) + " MHz");
	drawLabelValue(6, "VAlim : ", String(TensionBatterie/1000,2) +" V");

	char time_str[15];
	const uint32_t millis_in_day = 1000 * 60 * 60 * 24;
	const uint32_t millis_in_hour = 1000 * 60 * 60;
	const uint32_t millis_in_minute = 1000 * 60;
	uint8_t days = millis() / (millis_in_day);
	uint8_t hours = (millis() - (days * millis_in_day)) / millis_in_hour;
	uint8_t minutes = (millis() - (days * millis_in_day) - (hours * millis_in_hour)) / millis_in_minute;
	sprintf(time_str, "%2dj%2dh%2dm", days, hours, minutes);
	drawLabelValue(7, "Uptime : ", time_str);

	ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
	temp  = soft.substring(0,16);														// version soft ecran						
	temp += String(ver);
	ui.setTextAlignment(LEFT);
	ui.drawString(10, 250, temp);
	ui.setTextAlignment(CENTER);
	ui.drawString(117, 275, ville[1][config.city]);
	ui.setTextColor(ILI9341_WHITE, ILI9341_BLACK);	
	temp = F("Parametres Ecran Meteo");
	ui.drawString(117, 300, temp);	
}
//----------------------------------------------------------------------------------------------//
void draw_ecran41(byte zzone){// partie basse ecran 4
	/* zzone = 1 gauche, 2 centre, 3 droite
	on ecrit au centre la liste des villes dispo, actuelle couleur bleue
	de part  et d'autre une fleche montante descendante
	on selectionne en touchant le centre et sortie */

	static byte numlign = 0;
	if (zzone == 2){// enregistrement nouvelle ville de reference
		
		if(numlign + 1 <= nbrVille && numlign + 1 != config.city && numlign + 1 !=0){
			config.city = numlign + 1; // nouvelle ville
			EEPROM.put(0,config);
			EEPROM.commit();
			delay(100);
			EEPROM.get(0,config);
			delay(100);
			Serial.print(ville[1][config.city]);
			numlign = 0;
			updateData();
			updateForecast();
			ecran = 0;
			draw_ecran0();
			goto fin;
		}
	}
	else if (zzone == 1){
		if(numlign > 0) numlign --;
	}
	else if (zzone == 3){
		if(numlign < nbrVille - 1) numlign ++;
	}

	Serial.print(F("zzone :")),Serial.println(zzone);
	Serial.print(F("numligne :")),Serial.println(numlign);

	tft.fillRect(0, 234, tft.width(), 86,ILI9341_BLACK); // efface existant
	ui.drawBmp("/s" + extBmp , 20 , 245);// descendre icone vent sud
	ui.drawBmp("/n" + extBmp , 164, 245);// monter    icone vent nord

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
	static boolean first = true;
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
				
			}
			if (ecran == 4){
				if(first){
					first = false;
					draw_ecran41(0); // pas de selection ville au premier passage
				}
				else{
					draw_ecran41(2); // selection ville
					first = true;    // revalide premier passage
				}
			}
			break;
		
	}
}
//----------------------------------------------------------------------------------------------//
void draw_ecran(byte i){
	/* i = ecran */
	switch(i){
		case 0:
			draw_ecran0();
			break;
		case 1:
			draw_ecran1();
			break;
		case 2:
			draw_ecran2();
			break;
		case 3:
			if(config.UseMaMeteo && ville[2][config.city] != "0"){
				draw_ecran3();
				break;
			}
			else{ //suivant
				break;
			}
		case 4:
			draw_ecran4();
			break;			
	}
}
//----------------------------------------------------------------------------------------------//
void MajSoft() {
  /* cherche si mise à jour dispo et maj */
  
  String Fichier = "http://";
  Fichier += monSite;
  Fichier += "/bin/" ;
  Fichier += soft;
  Fichier += String (ver + 1); // cherche version actuelle + 1
  Fichier += ".bin";
  t_httpUpdate_return ret = ESPhttpUpdate.update(Fichier);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      //Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      Serial.println(F("Pas de mise à jour disponible !"));
      break;
    case HTTP_UPDATE_NO_UPDATES:
      //Serial.println(F("HTTP_UPDATE_NO_UPDATES"));
      break;
    case HTTP_UPDATE_OK:
      //Serial.println(F("HTTP_UPDATE_OK"));
      break;
  }
}

//--------------------------------------------------------------------------------//
String getTime(time_t *timestamp) {
	struct tm *timeInfo = localtime(timestamp);
	char buf[6];
	sprintf(buf, "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
	return String(buf);
}
//--------------------------------------------------------------------------------//
void Recordtempj(){
	/* Enregistre file SPIFF data du jour */
	File f = SPIFFS.open(FileDataJour, "w");
	f.println(tempj.tempmin);
	f.println(tempj.tempmax);
	f.close();  //Close file
}
//--------------------------------------------------------------------------------//
void updateMinMax(){
	boolean flagRecord = false;
	if(currentWeather.temp < tempj.tempmin){
		// Serial.print(F("Temp min act =")),Serial.println(currentWeather.temp);
		// Serial.print(F("Temp min rec =")),Serial.println(tempj.tempmin);
		tempj.tempmin = currentWeather.temp;
		flagRecord = true;		
	}
	if(currentWeather.temp > tempj.tempmax){
		// Serial.print(F("Temp max act =")),Serial.println(currentWeather.temp);
		// Serial.print(F("Temp max rec =")),Serial.println(tempj.tempmax);
		tempj.tempmax = currentWeather.temp;
		flagRecord = true;		
	}
	if(flagRecord){
		Recordtempj();
		// Serial.println(F("record data jour"));
		flagRecord = false;
	}
}
//--------------------------------------------------------------------------------//
void updateTime(){
	drawProgress(10, "Maj Heure");
	configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
	while(!time(nullptr)) {
		Serial.print("#");
		delay(100);
	}
	// calculate for time calculation how much the dst class adds.
	dstOffset = UTC_OFFSET * 3600 + dstAdjusted.time(nullptr) - time(nullptr);
	// Serial.printf("Time difference for DST: %d\n", dstOffset);
}
//--------------------------------------------------------------------------------//
void updateForecast(){
	drawProgress(70, "Maj previsions...");
	OpenWeatherMapForecast *forecastClient = new OpenWeatherMapForecast();
	forecastClient->setMetric(IS_METRIC);
	forecastClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
	uint8_t allowedHours[] = {12, 0};
	forecastClient->setAllowedHours(allowedHours, sizeof(allowedHours));
	forecastClient->updateForecastsById(forecasts, Openweathermap_key[API_KEY_Nbr],ville[0][config.city], MAX_FORECASTS);
	delete forecastClient;
	forecastClient = nullptr;
}
//--------------------------------------------------------------------------------//
void drawLabelValue(uint8_t line, String label, String value) {
  const uint8_t labelX = 15;
  const uint8_t valueX = 150;
  ui.setTextAlignment(LEFT);
  ui.setTextColor(ILI9341_LIGHTGREY, ILI9341_BLACK);
  ui.drawString(labelX, 110 + line * 15, label);
  ui.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  ui.drawString(valueX, 110 + line * 15, value);
}
//--------------------------------------------------------------------------------//
void loadFileSpiffs(){ // verification fichiers en SPIFFS et telechargement si absent
	/* temps de chargement si tout vide = 2mn30
		si existe = 0.06s */

	if(SPIFFS.exists(FileChk)){// verification du fichier de verification
		File f = SPIFFS.open(FileChk, "r");
		long s = f.readStringUntil('\n').toInt();
		Serial.print(F("Size lu fichier = ")),Serial.println(s);
		Serial.print(F("Size lu en mem  = ")),Serial.println(FileSize);
		if(s == FileSize){			
			Serial.println(F("Fichiers SPIFFS existant"));
			f.close();
			return; // fichier OK retour sans rien faire
		}
		Serial.println(F("erreur size Fichiers"));// fichiers manquant on format et download
		SPIFFS.format();
	}
	else{
		Serial.println(F("Fichiers SPIFFS absent"));
		SPIFFS.format();
	}

	String url = F("http://");
	url += wwwmonSitelocal;
	url += "/iconemeteo";
	static long size = 0; // size cumulé des fichiers pour verification
	String filename = "";

	for(int i = 0; i < 17 ; i++){ // icones system
		size += downloadFile(url,"/" + IconSystem[i] + extBmp);
	}
	for (int i = 0; i < 24; i++ ){ // icones lune
		size += downloadFile(url,"/moon" + String(i) + extBmp);
	}
	String liste[13]={"01d","01n","02d","02n","03d","04d","04n","09d","10d","11d","13d","50d","0"};
	for(int i = 0; i < 13; i++){ // icones meteo
		size += downloadFile(url,"/" + String(getMeteoconIcon(liste[i])) + extBmp);
	}
	for(int i = 0; i < 13; i++){ // mini icones meteo
		size += downloadFile(url,"/mini/" + String(getMeteoconIcon(liste[i])) + extBmp);
	}
	Serial.print(F("Size Cumul = ")),Serial.println(size);
	File f = SPIFFS.open(FileChk, "w");// création du fichier de verification
	f.println(size);
	f.close();
}

//--------------------------------------------------------------------------------//
long downloadFile(String url, String filename){

	long lenSPIFFS = 0;
	
	if (SPIFFS.exists(filename) == true) {
		Serial.print(filename),Serial.println(F(", File already exists. Skipping"));
		return lenSPIFFS;
	}

	if((WiFi.status() == WL_CONNECTED)) { // wait for WiFi connection		 WiFiMulti.run() == WL_CONNECTED

		HTTPClient http;

		Serial.print(F("[HTTP] begin...\n"));

		// configure server and url
		http.begin(url + filename);

		Serial.print(F("[HTTP] GET...\n"));
		// start connection and send HTTP header
		int httpCode = http.GET();
		if(httpCode > 0) {
			// HTTP header has been send and Server response header has been handled
			Serial.printf("[HTTP] GET... code: %d\n", httpCode);
			
			File f = SPIFFS.open(filename, "w");//w+
			if (!f) {
				Serial.println(F("file open failed"));
				return lenSPIFFS;
			}

			// file found at server
			if(httpCode == HTTP_CODE_OK) {

				// get lenght of document (is -1 when Server sends no Content-Length header)
				int len = http.getSize();
				// Serial.print("Long = "),Serial.print(len);

				// create buffer for read
				uint8_t buff[128] = { 0 };

				// get tcp stream
				WiFiClient * stream = http.getStreamPtr();

				// read all data from server
				while(http.connected() && (len > 0 || len == -1)) {
					// get available data size
					size_t size = stream->available();

					if(size) {
						// read up to 128 byte
						int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));

						// write it to Serial
						// Serial.write(buff, c);
						f.write(buff,c);

						if(len > 0) {
							len -= c;
						}
					}
					delay(1);
				}

				Serial.println();
				Serial.print(F("[HTTP] connection closed or file end.\n"));

			}
			lenSPIFFS = f.size();
			// Serial.print("exist size "),Serial.println(lenSPIFFS);
			f.close();
		} else {
			Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
		}
		http.end();
	}
	return lenSPIFFS;
}
