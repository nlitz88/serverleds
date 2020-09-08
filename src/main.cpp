#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>




// Makes HTTP Request to API and returns JSON response in form of string
String jsonFromRequest(String host, String call);

// "Deserializes" string json from jsonFromRequest and turns it into a DynamicJsonDoc (Obj)
DynamicJsonDocument getJsonObject(String json);


// Network variables
const String ssid = "state";
const String password = "litzinger2";


// System Specific Variables
const int CORES = 12;   // acts as max load


// Values for API Requests
const int REQUEST_INTERVAL = 5000;
unsigned long lastRequestTime = 0;

String NETDATA_HOST = "192.168.0.51:19999";
const String API_CALL = "/api/v1/data?chart=system.load&after=-5&points=1&format=json&dimension=load1";
String json;


// Load Variables
double loadValue;
double loadRatio;   // percentage of current load to max


// Values for incrementing hue
const int TRANSITION_INTERVAL = 5000;
unsigned long lastIncrementTime = 0;
const int FPS = 60;


// Hue values
uint8_t maxHue = 110;      // Distance between aqua and red. Could change to distance from aqua to orange if desired.
uint8_t currentHue = 0;
uint8_t newHue;
uint8_t lastHue = currentHue;
int deltaHue;           // Not unsigned; can be negative (negative change)
int hueIncrement;
bool overloaded = false;    // controls whether or not the brightness pulsates if system overloaded


// CRGB Values for blending
CRGB currentColor;
CRGB lastColor = CRGB(255, 0, 0);     // initialize last color and target as red (0) (arbitrary)
CRGB targetColor = CRGB(255, 0, 0);
// Value that controls how much color will be blended with next consecutive color each time.
int colorPrecision;
// Value that dictates how many fractions of 255/colorPrecision are given to the blend function.
// ex. 255/8 * 4 would indicate that blend would yield a color halfway between the last and target, as 255/8 * 4 = 255/2. 8 is CP, 4 is fractions
int fractions = 1;


// LED VARIABLES
#define NUM_LEDS 10
#define DATA_PIN 9
#define BRIGHTNESS 200
#define FRAMES_PER_SECOND 24
// Define the array of leds
CRGB leds[NUM_LEDS];
uint8_t brightness = BRIGHTNESS;


void setup() {

  Serial.begin(115200);

  // Connect to wifi network
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected! IP Address: ");
  Serial.println(WiFi.localIP());

  // setup LEDS
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  // FastLED.setBrightness(BRIGHTNESS); Brightness incorporated in hsv2rgb_rainbow function below.

  delay(500);

}

int frames = 0;

void loop() {


  // A: EVERY REQUEST INTERVAL, GET NEW DATA, AND SET INCREMENTING VARIABLES ACCORDINGLY
  if(millis() - lastRequestTime >= REQUEST_INTERVAL) {

    lastRequestTime = millis();


    // 1ST: GET JSON FROM API REQUEST
    json = jsonFromRequest(NETDATA_HOST, API_CALL);


    // 2ND: PARSE JSON FOR LOAD VALUE
    DynamicJsonDocument response = getJsonObject(json);
    // Extract loadValue from Json object
    loadValue = response["data"][0][1];
    Serial.println(String("Time: ") + millis() + String(" | LoadValue: ") + loadValue);


    // 3RD: Set currentHue = lastHue, as it may never reach the exact hue it was supposed to. This way, it can start at the proper value and increment from there.
    currentHue = lastHue;
    Serial.println(String("Time: ") + millis() + String(" | corrected currentHue (starts at this hue): ") + (maxHue - currentHue));
    

    // 4TH: Calculate ratio of new loadValue to max loadValue
    loadRatio = loadValue / CORES;


    // 5TH: Determine new Hue and reset lastHue. Then determine how much hue must change (in 5 seconds and each "frame").
    // This condition prevents the color from going beyond the hue limit
    if(loadRatio * maxHue <= maxHue) {
      newHue = loadRatio * maxHue;
      overloaded = false;
    }
    else if(deltaHue > 10) {    // if the hue change is very dramatic, system is likely overloaded
      newHue = maxHue;
      overloaded = true;
    }
    else {
      newHue = maxHue;
      overloaded = true;
    }

    deltaHue = newHue - lastHue;
    lastHue = newHue;
    


    // 6TH: Calculate hueIncrement AND colorPrecision for the next Request Interval
    // hueIncrement will either be +/-1 or a value greater than +/-1.
    // Color precision will be > 1  in normal circumstances, but in those where deltaHue > FPS * interval, cp = 1 and hueIncrement > 1
    if(abs(deltaHue) <= FRAMES_PER_SECOND * (REQUEST_INTERVAL / 1000) && deltaHue != 0) {
      hueIncrement = deltaHue / abs(deltaHue);
      colorPrecision = (FPS * (TRANSITION_INTERVAL / 1000)) / abs(deltaHue);
    }
    else if(deltaHue == 0) {
      hueIncrement = 0;
      colorPrecision = 255;   // Default value. Doesn't actually even get utilized during execution, as there is no hue change.
    }
    else {
      hueIncrement = deltaHue / (FPS * (TRANSITION_INTERVAL / 1000));
      colorPrecision = 1; // OR (FPS * (TRANSITION_INTERVAL / 1000)) / abs(deltaHue) * hueIncrement;
      Serial.println(String("deltaHue > FPS * TRANSITION_INTERVAL. Hue Increment = ") + hueIncrement);
    }


    Serial.println(String("Time: ") + millis() + String(" | New Hue: ") + loadRatio + String(" x " ) + maxHue + String(" = ") + newHue);
    Serial.println(String("Time: ") + millis() + String(" | deltaHue: ") + deltaHue + String(" | Hue Increment: ") + hueIncrement);
    Serial.println(String("Time: ") + millis() + String(" | Color Precision: ") + colorPrecision);
    Serial.println();
    

  }


  
  // B: EVERY 1000/FPS, UPDATE / INCREMENT LEDS
  if (millis() - lastIncrementTime >= 1000 / FPS) {

    lastIncrementTime = millis();

    if(currentHue != newHue) {

      ++frames;
    
      // 1ST: CALCULATE BLENDED COLOR
      currentColor = blend(lastColor, targetColor, (255/colorPrecision) * fractions++);
      // Determine if hue needs to be incremented or not
      if(fractions > colorPrecision) { // and in our case, only do this if the hue isn't already where it needs to be
      
        Serial.println(String("fractions: ") + fractions + String(" is greater than colorPrecision: ") + colorPrecision);
        fractions = 1;
        lastColor = targetColor;

        // either adding or subtracting by hueIncrement
        if(currentHue + hueIncrement > newHue) {
          currentHue += hueIncrement / abs(hueIncrement);  // add 1 or -1
        }
        else {
          currentHue += hueIncrement;
        }

      }

      // print out current color values in r g b to determine if it can set r g b as floating point somehow, or to observe how much it can actually change at once;
      Serial.println(String("Time: ") + millis() + String(" | Frames: ") + frames + String(" | Hue: ") + (maxHue - currentHue) + String(" | Fractions: ") + fractions);

    }
    
    // 2ND: PULSATE IF SERVER OVERLOADED
    // could also make it do this 
    if(overloaded) {
      brightness = beatsin8(40, 70, 255);
    }
    else {
      brightness = BRIGHTNESS;
    }

    hsv2rgb_rainbow(CHSV(maxHue - currentHue, 255, brightness), targetColor);


    // THIS SHOULD BE REPLACED. IF THE SYSTEM IS OVERlOADED, IT SHOULD STILL IDEALLY BE USING THE COLORPRECISION TO FADE BETWEEN BRIGHTNESS VALUES. FIGURE THIS OUT AT A LATER DATE, as I'll need this to determine 
    // Currently configured, it should only bypass the precise animation if it's already at its peak color and no hue transitioning will be encountered. This should however, result in a noticable difference in the pulsing rate beteween overload during hue shifts and overlad at maxHue.
    fill_solid(leds, NUM_LEDS, overloaded && currentHue == maxHue ? targetColor : currentColor);
    FastLED.show();


  }

}


String jsonFromRequest(String host, String call) {
  
  // Create instance of httpclient class
  // This class allows us to make http calls (get, post, etc). More easily
  HTTPClient http;

  // Initialize http instance to make calls to the API at this endpoint:
  http.begin("http://" + host + call);

  // Makes a GET request for the data at the url 
  // Just GET(), however, only returns the status of the request. If it's > 0, then some communication was made
  int httpCode = http.GET();

  String payload = "{}";

  if(httpCode > 0) {
    payload = http.getString();
    //    Serial.println(payload);
    
    //    Serial.print("Time elapsed: ");
    //    Serial.println(millis()/1000);
  }
  else {
    Serial.println(httpCode);
  }

  http.end();

  return payload;

}


DynamicJsonDocument getJsonObject(String json) {
  
  // Capacity/size calculation for JSON object (from API ArduinoJSON Assistant)
  const size_t capacity = JSON_ARRAY_SIZE(1) + 2*JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(2) + 30;
  // Define new dynamic JSON object with size capacity called response
  DynamicJsonDocument response(capacity);

  // Then, deserialize the response
  // This takes our response in string form and lets us access/manipulate the response as an object
  deserializeJson(response, json);

  return response;
  
}