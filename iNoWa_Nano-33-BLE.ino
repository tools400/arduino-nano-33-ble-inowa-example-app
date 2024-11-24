//##################################################################
//#                  iNoWa - the indoor NorthWall                  #
//##################################################################
//  NANO-BLE Firmware für die
//  Steuerung des LED Systems
//
//  by Constantin Hagen, 2021
//  Open Source! GNU GPL v3.0
//
//  Version 1.1.6
//  20.02.2022
//
//  Version 2.1.0-20241124-1639 by Thomas Raddatz
//                                 (thomas.raddatz@tools400.de)
//  24.11.2024
//  Changed: Changed program to run on Arduino Nano 33 BLE
//##################################################################
//  App Command:
//  The command sent by the Android app has the following format:
//    <led_num>:<color>/<brightness>!<effects><eol>
// 
//    led_num    - LED number, starting at 1.
//                 Range: 1 - NUM_LEDS
//    color      - Color.
//                 Possible values:
//                   r=red, g=green, b=blue or l=purple.
//    brightness - Brightness.
//                 Must be between 0 (min) and 255 (max).
//    effects    - Optional. Special effects.
//                 Possible values:
//                   P=Pulse (fading) or S=Sparkle (glitter).
//    eol        - End of line.
//                 Possible values:
//                   #=Boulder or *=Snake game
//
//  Buttons:
//  Red button   - Reset.
//  Green button - Mode switch. Modes:
//                   0=Off. All LEDs are switched off.
//                   1=Red. All LEDs are set to RED.
//                   2=Green. All LEDs are set to GREEN.
//                   3=Blue. All LEDs are set to BLUE.
//                   4=Rainbow. All LEDs display rainbow colors.
//                   5=Boulder. LEDs are set according to the last app command.
//
//  Limitations:
//  The maximum number of boulder moves are defined by I_MAX_MOVES.
//
//  Error Handling:
//  In case of an error, the application sets all LEDs of the strip
//  to RED and flashes the whole strip 3 times.
//  Then the first LED is set to RED.
//  The error number is displayed by n yellow LEDs.
//
//  Questions:
//  1. What is the '.' used for in the incoming app command?
//##################################################################

// External libraries
#include <Adafruit_NeoPixel.h>
#include <ArduinoBLE.h>
#include <EasyButton.h>
#include <Random16.h>

Random16 rnd16;
#define random16 rnd16.get

// Hardware settings.
#define READY_LED            2    // Pin of "Power-On" LED.
#define LED_STRIPE_PIN      13    // Pin of 'WS2811' LED stripe.
#define TEST_BUTTON_PIN     11    // Test Button.
#define SPEAKER_PIN         12    // Loudspeaker (buzzer).
#define NUM_LEDS           200    // Number of LEDs of the LED stripe.
#define REVERSE_ORDER        0    // Reverse order of LEDS. 1=reverse order, 0=default order

Adafruit_NeoPixel leds(NUM_LEDS, LED_STRIPE_PIN, NEO_GRB + NEO_KHZ800);
// Argument 1 = Number of pixels in NeoPixel leds
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)

EasyButton testButton(TEST_BUTTON_PIN, 50, true, true); // pullup & invert -> true; debounce: 50ms

// Bluetooth Configuration
#define MAX_SIZE_APP_CMD      128
//BLEService ledService("19B10010-E8F2-537E-4F6C-D104768A1214"); // create service
//BLEStringCharacteristic  ledCharacteristic("19B10011-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, MAX_SIZE_APP_CMD);
BLEService ledService("0000FFE0-0000-1000-8000-00805F9B34FB"); // create service
BLEStringCharacteristic  ledCharacteristic("0000FFE1-0000-1000-8000-00805F9B34FB", BLERead | BLEWrite, MAX_SIZE_APP_CMD);

// NeoPixel LED colors
#define NP_OFF      leds.Color(0, 0, 0)
#define NP_GREEN    leds.Color(255, 0, 0)
#define NP_RED      leds.Color(0, 255, 0)
#define NP_BLUE     leds.Color(0, 0, 255)
#define NP_WHITE    leds.Color(255, 255, 255)
#define NP_PURPLE   leds.Color(0, 128, 128)
#define NP_YELLOW   leds.Color(255, 228, 0)

// App command markers
#define COLOR_SEPARATOR       ':' // Number separator. The following byte is a character.
#define HOLD_SEPARATOR        '/' // Character separator. The following byte is a number.
#define PULSE_MARKER          'P' // Enabled fading if set to 'P'.
#define SPARKLE_MARKER        'S' // Enabled glittering if set to 'S'.
#define BRIGHTNESS_MARKER     '!' // Starts the brightness value.
#define EOL_SNAKE_GAME        '*' // End of transmission of Snake game.
#define EOL_BOULDER           '#' // End of transmission of boulder.
#define RED                   "r" // Red
#define BLUE                  "b" // Blue
#define GREEN                 "g" // Green
#define PURPLE                "l" // Purple
#define SIZE_APP_COMMAND      512 // Maximum size of a command sent by the Android app.

// Configuration values
#define BRIGHTNESS_TEST       127 // Brightness for startup blink test.
#define BRIGHTNESS_BOULDER    200 // Brightness for boulder moves.
#define BRIGHTNESS_MIN        100 // Minimum brightness, if fading is enabled.
#define BRIGHTNESS_MAX        255 // Maximum brightness, if fading is enabled.
#define FADE_AMOUNT           5   // Amount of fading. Original value: 5
#define FADE_SPEED            20  // Fading speed. Low values = fast, hight values = slow. Original value: 9

#define RAINBOW_FIRST_HUE     0   // Hue of first pixel, 0-65535, representing one full cycle of the color wheel.
#define RAINBOW_REPS          7   // Number of cycles of the color wheel over the length of the strip.
#define RAINBOW_SATURATION    255 // Saturation (optional)
#define RAINBOW_BRIGHTNESS    255 // Brightness (optional)
#define RAINBOW_GAMMIFY       true// If true (default), apply gamma correction to colors for better appearance.

// Error status
#define E_ERROR_FLAG_LED      0   // Error indicator LED.
#define E_ERROR_OFFS_LED      2   // Offset error indicator LEDs.
#define E_TOO_MANY_MOVES      1   // Error: Too many moves.
#define E_INTERVALL           500 // Error flag flashing interval in milliseconds.
#define NP_E_ERROR      NP_RED    // Color of the error LED at position 0.
#define NP_E_ERRNUM     NP_YELLOW // Color of the error number LED at position 2-n.

// Application Status Variables.
bool isEventLoopMessage = false;  // Ensures message is sent only once.
bool glitterEnabled = false;      // Enable glitter effect.
bool fadingEnabled = false;       // Enable fadingEnabled effect.
int fadeDirection = -1;           // Fade down.
int brightness = BRIGHTNESS_MAX;  // Set max. brightness for fadingEnabled effect.
int testButton_counter = 0;       // Test button counter for switching mode.
bool isErrorState = false;        // Error indicator. Stops processing commands.

typedef struct {
  char appCommand[SIZE_APP_COMMAND];
  int appCmdLen;
  int appCmdOffs;
} Ble;

Ble ble;

// Variables for reading data from the Bluetooth interface.
#define I_LED                 0   // Index of LED number property.
#define I_COLOR               1   // Index of COLOR code property.
#define I_MAX_MOVES           25  // Maximum number of boulder moves.
#define I_MAX_LED_PROPS       2   // Maximum number of LED configuration properties.

typedef struct {
  String value = "";              // Incoming value of Android app command.
  int moveNum = 0;                // Number of boulder move of Android app command.
  String LEDConfig
          [I_MAX_MOVES]
          [I_MAX_LED_PROPS];       // Array to store a LED configuration properties of Android app command.
  char pulseMarker = ' ';         // Incoming marker for fading effect. 
  char sparkleMarker = ' ';       // Incoming marker for glitter effect.
  int brightness = 0;             // Incoming brightness value.
} Config;

Config currentConfig;

// =================================================================
//  Standard Arduino application methods.
// =================================================================

/**
 * Setup procedure. Executed once on application start.
 */
void setup() {

  // Enable debugging
  Serial.begin(9600); // Any baud rate should work
  delay(500);
  //while (!Serial); // Works only if USB cable is connected and Arduino IDE is started

  log("Starting setup...");

  // Initialize LED strip.
  leds_initialize();

  // Initialize Bluetooth service.
  Bluetooth_initialize();

  // Prepare status LED.
  pinMode(READY_LED, OUTPUT);
 
  // Test all lEDs at startup.
  startupLEDsTest();

  // Play welcome melody.
  playWelcomeMelody();

  // Register listener for "Mode" button.
  testButton.begin();
  testButton.onPressed(testButton_pressed);
  testButton_counter = 0;
}

/**
 * Main event loop.
 */
void loop() {

  // poll for Bluetooth® Low Energy events
  BLE.poll();

  // Signal "Power-On".
  digitalWrite(READY_LED, HIGH);

  if (!isEventLoopMessage) {
    log("Starting event loop...");
    isEventLoopMessage = true;
  }

  // Read the TEST button.
  testButton.read();

  // Receive data from Android app.
  // Stop execution, if application is in error state.
  receiveAppCommand();

  // Optionally add some glitter effect.
  if (glitterEnabled == true){
    addGlitterEffect(30);
  }

  // fadingEnabled signal for ambience light.
  if (fadingEnabled == true){
    addFadingEffect();
  }

}

// =================================================================
//  Methods for handling Android app commands.
// =================================================================

/**
 * Receives and processes a command send by the Android app.
 */
void receiveAppCommand() {

  if (isErrorState) {
    return;
  }

  // Try to read some data from the Bluetooth interface.
  AppCommand_tryReceiveAppCommand();

  if (AppCommand_available()) {

    delay(20); // just wait a little bit for more characters to arrive

    // Reset input buffer.
    in_reset();

    // Temporary buffer for building an input token.
    String inToken = "";

    // Parse incomming app command:
    while (AppCommand_available()) { // Bluetooth Low Energy

      //TODO: receive data from serial or Bluetooth

      char inByte = AppCommand_readNext();

      // Consume data bytes for collecting inToken bytes. 
      if (!isControlByte(inByte)) {
        inToken.concat(inByte);
      }

      // Store numeric LED number.
      if (inByte == COLOR_SEPARATOR) {
        currentConfig.LEDConfig[currentConfig.moveNum][I_LED] = inToken;
        inToken = "";
      }

      // Store alpha-numeric color indicator: 'r', 'g' or 'b'.
      if (inByte == HOLD_SEPARATOR) {
        currentConfig.LEDConfig[currentConfig.moveNum][I_COLOR] = inToken;
        inToken = "";
        currentConfig.moveNum++;
        if (currentConfig.moveNum > I_MAX_MOVES) {
          signalErrorState(E_TOO_MANY_MOVES);
          return;
        }
      }

      // 'P': pulsate marker for ambience light mode (fading).
      if (inByte == PULSE_MARKER) {
        currentConfig.pulseMarker = inByte;
        inToken = "";
      }

      // 'S': sparkle marker for glitter mode.
      if (inByte == SPARKLE_MARKER) {
        currentConfig.sparkleMarker = inByte; // 'S'
        inToken = "";
      }

      // Store brightness value.
      if (inByte == BRIGHTNESS_MARKER) {
        currentConfig.brightness = inToken.toInt();
        inToken = "";
      }

      // End of App command.
      if (isEOL(inByte)) {

        configureLEDs();

        if (inByte == EOL_BOULDER) {
          playBoulderMelody();
        } else {
          playSnakeMelody();
        }
      }
    }
  }
}

/**
 * Configures the LEDs according to the app command last received.
 */
void configureLEDs() {

  // Set LED colors
  log("Configuring " + String(currentConfig.moveNum) + " LEDs...");

  // Turn off all LEDs.
  leds_off();

  // Set brightness.
  brightness = currentConfig.brightness;
  leds_brightness(brightness);

  // Configure LED byLED...
  for (int m = 0; m < currentConfig.moveNum; m++) {
    int led = currentConfig.LEDConfig[m][I_LED].toInt() - 1;
    String color = currentConfig.LEDConfig[m][I_COLOR];
    if (color == RED) {
      log("Boulder: Set color of target handle (red).");
      leds_setPixelColor(led, NP_RED);    // target handle
    } else if (color == GREEN) {
      log("Boulder: Set color of starting handle (green).");
      leds_setPixelColor(led, NP_GREEN);  // starting handle
    } else if (color == BLUE) {
      log("Boulder: Set color of intermediate handle (blue).");
      leds_setPixelColor(led, NP_BLUE);   // intermediate handle
    } else if (color == PURPLE) {
      log("Boulder: Set color of intermediate handle (purple).");
      leds_setPixelColor(led, NP_PURPLE); // intermediate handle
    }
  }

  // Enable fading.
  if (currentConfig.pulseMarker == PULSE_MARKER) {
    log("Fading: enabled");
    fadingEnabled = true;
  } else {
    log("Fading: disabled");
    fadingEnabled = false;
  }

  // Enable glitter.
  if (currentConfig.sparkleMarker == SPARKLE_MARKER) {
    log("Glitter: enabled");
    glitterEnabled = true;
  } else {
    log("Glitter: disabled");
    glitterEnabled = false;
  }

  // Turn LEDs on, displaying boulder moves or snake game.
  leds_show();
}

/**
 * Add a glitter effect.
 */
void addGlitterEffect( int aChanceOfGlitter) {

  if (random(0, 256) < aChanceOfGlitter) {
    leds_setPixelColor(random16(NUM_LEDS), NP_WHITE);
    leds_show();
  }
}

/**
 * Add fading effect for ambience light.
 */
void addFadingEffect() {

  log("Adding fading effect...");

  leds_brightness(brightness);
  leds_show();

  brightness = brightness + (FADE_AMOUNT * fadeDirection);

  // Reverse the direction of the fading at the ends of the fade:
  if (brightness <= BRIGHTNESS_MIN || brightness >= BRIGHTNESS_MAX){
    fadeDirection = fadeDirection * -1;
  }
  delay(FADE_SPEED);
}

/**
 * Tests an incoming byte.
 * Returns 'true', if the incoming byte is a control byte, else 'false'.
 */
bool isControlByte(char anInByte) {

  if (isEOL(anInByte)
      || anInByte == HOLD_SEPARATOR
      || anInByte == COLOR_SEPARATOR
      || anInByte == BRIGHTNESS_MARKER
      || anInByte == SPARKLE_MARKER
      || anInByte == PULSE_MARKER
      || anInByte == '.') {  //TODO: Dot is actually not used. Indented usage: unknown
    return true;
  }

  return false;
}

/**
 * Tests an incoming byte.
 * Returns 'true', if the incoming byte indicates the end of the line (command).
 */
bool isEOL(char anInByte) {

  if (anInByte == EOL_BOULDER || anInByte == EOL_SNAKE_GAME) {
    return true;
  }

  return false;
}

/**
 * Resets the variables for reading data from the Bluetooth interface.
 */
void in_reset() {

  currentConfig.moveNum = 0;
  currentConfig.pulseMarker = ' ';
  currentConfig.sparkleMarker = ' ';
  currentConfig.brightness = BRIGHTNESS_BOULDER;

  for (int m=0; m < I_MAX_MOVES; m++) {
    for (int p=0; m < I_MAX_LED_PROPS; m++) {
      currentConfig.LEDConfig[m][p] = "";
    }
  }
}

// =================================================================
//  Methods for handling user input (mode button pressed).
// =================================================================

/**
 * Listener: Fired on pressing the "green" TEST button.
 */
void testButton_pressed() {

  testButton_counter++;
  log("testButton_counter is: " + String(testButton_counter));

  if (testButton_counter > 7) {
    log("Switching all LEDs off...");
    testButton_counter = 0;
    leds_off();
    signalMode();
    delay(100);
    signalMode();
  }
  else if (testButton_counter == 1) {
    log("Switching all LEDs to RED...");
    leds_brightness(BRIGHTNESS_TEST);
    leds_on(NP_RED);
    signalMode();
  }
  else if (testButton_counter == 2) {
    log("Switching all LEDs to GREEN...");
    leds_brightness(BRIGHTNESS_TEST);
    leds_on(NP_GREEN);
    signalMode();
  }
  else if (testButton_counter == 3) {
    log("Switching all LEDs to BLUE...");
    leds_brightness(BRIGHTNESS_TEST);
    leds_on(NP_BLUE);
    signalMode();
  }
  else if (testButton_counter == 4) {
    log("Switching all LEDs to PURPLE...");
    leds_brightness(BRIGHTNESS_TEST);
    leds_on(NP_PURPLE);
    signalMode();
  }
  else if (testButton_counter == 5) {
    log("Switching all LEDs to WHITE...");
    leds_brightness(BRIGHTNESS_TEST);
    leds_on(NP_WHITE);
    signalMode();
  }
  else if (testButton_counter == 6) {
    log("Displaying a rainbow...");
    leds_brightness(BRIGHTNESS_TEST);
    leds_rainbow();
    signalMode();
  }
  else if (testButton_counter == 7) {
    log("Displaying the boulder moves...");
    if (brightness == 0) {
      leds_brightness(BRIGHTNESS_BOULDER);
    }
    configureLEDs();
    playBoulderMelody();
  }
}

// =================================================================
//  Methods for the startup LED test.
// =================================================================

/**
 * Test LEDs on startup.
 */
void startupLEDsTest() {

  log("Starting LED test...");

  leds_off();
  leds_brightness(BRIGHTNESS_TEST);

  // Start blink test to confirm LEDs are working.
  leds_on(NP_RED);
  delay(1000);

  leds_on(NP_GREEN);
  delay(1000);

  leds_on(NP_BLUE);
  delay(1000);

  // Rainbow.
  leds_rainbow();
  delay(2000);

  leds_brightness(BRIGHTNESS_BOULDER);

  leds_off();
}

// =================================================================
//  Methods for audio output. Melodies and beeps.
// =================================================================

/**
 * Plays a short welcome melody at startup.
 */
void playWelcomeMelody() {

  log("Playing welcome melody...");

  tone(SPEAKER_PIN, 264);
  delay(300);
  tone(SPEAKER_PIN, 330);
  delay(300);
  tone(SPEAKER_PIN, 396);
  delay(450);
  noTone(SPEAKER_PIN);
}

 /**
 * Plays a short boulder melody.
 */
void playBoulderMelody() {

  log("Playing boulder melody...");

  tone(SPEAKER_PIN, 1000);
  delay(100);
  tone(SPEAKER_PIN, 2000); 
  delay(100); 
  noTone(SPEAKER_PIN); 
}

 /**
 * Plays a short melody fpr the Snake game.
 */
void playSnakeMelody() {

  log("Playing snake game melody...");

  tone(SPEAKER_PIN, 432);
  delay(100);
  noTone(SPEAKER_PIN);
}

/**
 * Produces a short keystroke beep.
 */
void signalMode() {

  tone(SPEAKER_PIN, 864);
  delay(250);
  noTone(SPEAKER_PIN);

  log("** Buzzer **");
}

/**
 * Signal an application error.
 */
void signalErrorState(int errorNumber) {

  log("Signaling error: " + String(errorNumber));

  isErrorState = true;

  in_reset();

  tone(SPEAKER_PIN, 50);
  delay(500);
  noTone(SPEAKER_PIN);

  // Blink all LEDs to show error status.
  leds_off();
  for (int i=0; i < 3; i++) {
    leds_on(NP_E_ERROR);
    delay(E_INTERVALL);
    leds_off();
    delay(E_INTERVALL);
  }

  // Show error number:
  leds_off();
  leds_setPixelColor(E_ERROR_FLAG_LED, NP_E_ERROR);
  leds_brightness(BRIGHTNESS_MAX);
  leds_show();
}

/**
 * Displays the current error number with n yellow LEDs.
 */
void displayErrorNumber(int anErrorNumber) {

  log("Displaying error number: " + String(anErrorNumber));

  for (int i=0; i < anErrorNumber; i++) {
    int led = E_ERROR_OFFS_LED + i;
    if (leds.getPixelColor(led) == NP_OFF) {
      leds_setPixelColor(led, NP_E_ERRNUM);
    } else {
      leds_setPixelColor(led, NP_OFF);
    }
    leds_brightness(BRIGHTNESS_MAX);
    leds_show();
  }
}

// =================================================================
//  Methods for encapsulating the NeoPixel library procedure calls.
// =================================================================

/**
 * Initializes the NeoPixel library.
 * Sets the number of LEDs and turns all LEDs off.
 */
void leds_initialize() {

  log("NeoPixel: Initializing library...");

  leds.begin();
  // leds.setMaxPowerInVoltsAndMilliamps(12,9240); // we have 154 12V pixels with max power consumption of 60mA per pixel (white)
  leds_off();
}

/**
 * Sets all LEDs to the same color.
 */
void leds_on(uint32_t aColor) {

  log("NeoPixel: Switching all LEDs on...");
  leds.fill(aColor, 0, NUM_LEDS);
  leds_show();
}

/**
 * Sets the color of the LED identified by
 * the specified index starting at 0.
 */
void leds_setPixelColor(unsigned int anIndex, uint32_t aColor) {

  if (REVERSE_ORDER == 1) {
    int reverseIndex = (anIndex - (NUM_LEDS - 1)) * -1;
    log("Reverse LED index: " + String(anIndex) + " -> " + String(reverseIndex));
    anIndex = reverseIndex;
  }

  log("NeoPixel: Setting pixel color of LED #" + String(anIndex) + " to color: " + String(aColor));
  leds.setPixelColor(anIndex, aColor);
}

/**
 * Sets all LEDs to rainbow colors.
 */
void leds_rainbow() {

  log("NeoPixel: Displaying rainbow...");
  leds.rainbow(RAINBOW_FIRST_HUE, RAINBOW_REPS, RAINBOW_SATURATION, RAINBOW_BRIGHTNESS, RAINBOW_GAMMIFY);
  leds_show();
}

/**
 * Sets the brightness of the LEDs.
 */
void leds_brightness(uint8_t aBrightness) {

  log("NeoPixel: Setting brightness to: " + String(aBrightness));
  leds.setBrightness(aBrightness);
}

/**
 * Turns all LEDs off.
 */
void leds_off() {

  log("NeoPixel: Clearing LEDs...");
  leds.clear();
  leds_show();
}

/**
 * Send LED configurations to LED stripe.
 */
void leds_show() {

  log("NeoPixel: Showing LEDs...");
  leds.show();
}

// =================================================================
//  Methods building a fassade for Arduino application.
//  These methods are called by the Arduino application. They
//  forward the request to the actual Bluetooth library.
// =================================================================

// Initializes the Bluetooth library.
// Called once, when the application starts.
void Bluetooth_initialize() {
  BluetoothLE_initialize();
}

// Receives a command from the Android app.
void AppCommand_tryReceiveAppCommand() {
  BluetoothLE_tryReceiveAppCommand();
}

// Tests whether new input data is available.
bool AppCommand_available() {
  // return BluetoothLE_available();
  return ble.appCmdOffs < ble.appCmdLen;
}

// Returns the next by of the current app command.
char AppCommand_readNext() {
  return BluetoothLE_read();
}

// =================================================================
//  Methods building a fassade for the BluetoothLE library.
// Replace this methods if you need to use another library and
// update the methods of the app fassade.
// =================================================================

void BluetoothLE_initialize() {

  if (!BLE.begin()) {
    log("Starting Bluetooth® Low Energy module failed!");
    while (1);
  }

  // Set Bluetooth application name.
  BLE.setLocalName("i-NoWa Indoor North Wall");

  // Set the UUID for the service this peripheral advertises:
  BLE.setAdvertisedService(ledService);

  // Add the characteristics to the service
  ledService.addCharacteristic(ledCharacteristic);

  // Add the service
  BLE.addService(ledService);

  // Start advertising
  BLE.advertise();

  log("Bluetooth® device active, waiting for connections...");
}

void BluetoothLE_tryReceiveAppCommand() {
  if (BLE.connected()) {
    if (ledCharacteristic.valueLength() > 0) {
      ble.appCmdLen = ledCharacteristic.readValue(ble.appCommand, SIZE_APP_COMMAND);
      ble.appCmdOffs = 0;
      ledCharacteristic.writeValue("");
      log("Received app command: " + String(ble.appCommand) + " (" + ble.appCmdLen + " bytes)");
    }
  } else {
    ble.appCmdLen = 0;
    ble.appCmdOffs = 0;
  }
}

bool BluetoothLE_available() {
  if (ble.appCmdOffs < ble.appCmdLen) {
    return true;
  } else {
    return false;
  }
}

char BluetoothLE_read() {
  char tChar = ' ';
  if (ble.appCmdOffs < SIZE_APP_COMMAND) {
    log("Current offset: " + String(ble.appCmdOffs));
    tChar = ble.appCommand[ble.appCmdOffs];
    ble.appCmdOffs = ble.appCmdOffs + 1;
  }
  if (ble.appCmdOffs >= ble.appCmdLen) {
    log("** No more input data available **");
  }
  return tChar;
}

// =================================================================
//  Methods for appending log messages to the serial monitor.
// =================================================================

void log(String aMessage) {
  Serial.println(aMessage);
}

void log(char aChar) {
  Serial.println(aChar);
}
