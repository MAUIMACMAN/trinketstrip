/*
 trinketstrip
 
 Have some fun with a Adafruit Trinket and NeoPixel LED strip.
 Push the button and cycle through all modes of the program.
 
 Hardware requiremetns:
 - Adafruit Trinket (ATTiny85)
( - Adafruit Electret Microphone Amplifier (ID: 1063)
   connected to Trinket pin #2 (analog A1)
DISCONNECTED AT THE MOMENT
)
   
 - Adafruit NeoPixel Digitial LED strip or anything like that
   connectet to Trinket pin #0
 - an IR receiver (TSOP 4838)
   connected to Trinket pin #2 and 5V

 Written by Stefan Scherer under the BSD license.
 This parapgraph must be included in any redistribution.
 */
 
/*
 VU meter
 Derived from http://learn.adafruit.com/trinket-sound-reactive-led-color-organ/code
 
 LED "Color Organ" for Adafruit Trinket and NeoPixel LEDs.
 
Hardware requirements:
- Adafruit Trinket or Gemma mini microcontroller (ATTiny85).
- Adafruit Electret Microphone Amplifier (ID: 1063)
- Several Neopixels, you can mix and match
o Adafruit Flora RGB Smart Pixels (ID: 1260)
o Adafruit NeoPixel Digital LED strip (ID: 1138)
o Adafruit Neopixel Ring (ID: 1463)
 
Software requirements:
- Adafruit NeoPixel library
 
Connections:
- 5 V to mic amp +
- GND to mic amp -
- Analog pinto microphone output (configurable below)
- Digital pin to LED data input (configurable below)
 
Written by Adafruit Industries. Distributed under the BSD license.
This paragraph must be included in any redistribution.
*/


#include <avr/power.h>
#include <Adafruit_NeoPixel.h>


#define N_PIXELS   80  // Number of pixels in strand
#define MIC_PIN    1  // Microphone is attached to this analog pin
#define STRIP_PIN  0  // NeoPixel LED strand is connected to this pin
#define DC_OFFSET  0  // DC offset in mic signal - if unusure, leave 0
#define NOISE    100  // Noise/hum/interference in mic signal
#define SAMPLES   30  // Length of buffer for dynamic level adjustment
#define TOP       (N_PIXELS + 1) // Allow dot to go slightly off scale
#define PEAK_FALL_MILLIS 10  // Rate of peak falling dot

#define DOT_RUN_MILLIS 20

#ifdef USE_BUTTON
#define BUTTON_PIN 1
#else
#define IR_PIN           2
#define IR_PIN_INTERRUPT PCINT2
#endif

#define LAST_PIXEL_OFFSET N_PIXELS-1

enum 
{
  MODE_OFF,
  MODE_VUMETER,
#ifdef DOT
  MODE_DOT_UP,
  MODE_DOT_DOWN,
  MODE_DOT_ZIGZAG,
#endif
  MODE_RAINBOW,
  MODE_RAINBOW_CYCLE,
  MODE_WIPE_RED,
  MODE_WIPE_GREEN,
  MODE_WIPE_BLUE,
  MODE_WIPE_YELLOW,
  MODE_WIPE_CYAN,
  MODE_WIPE_MAGENTA,
  MODE_WIPE_COLORS,
  MODE_MAX
} MODE;


byte mode = MODE_OFF;
byte reverse = 0;

byte
  peak      = 0,      // Used for falling dot
  volCount  = 0;      // Frame counter for storing past volume data
int
  vol[SAMPLES];       // Collection of prior volume samples
int
 lvl       = 10,      // Current "dampened" audio level
  minLvlAvg = 0,      // For dynamic adjustment of graph low & high
  maxLvlAvg = 512;
  
// Parameter 1 = number of pixels in strip
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(N_PIXELS, STRIP_PIN, NEO_GRB + NEO_KHZ800);

long lastTime = 0;

#ifdef USE_BUTTON
// Variables will change:
byte buttonState;             // the current reading from the input pin
byte lastButtonState = LOW;   // the previous reading from the input pin
// the following variables are long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
long lastDebounceTime = 0;  // the last time the output pin was toggled
#define DEBOUNCE_DELAY 50  // the debounce time; increase if the output flickers
#else


// After this much time has elapsed after the last transition, consider the
// message complete.
#define MSG_COMMIT_TIME_MICROSEC 30000

enum IRProtocolState { Idle, Building };

unsigned long timeOfLastIRPinChange;  // Time when the last pin change happened on the IR decoder.
unsigned long timeToCommitMessage;    // Time after which we should consider the message complete.
unsigned long message;                // Workspace where the message is built up.
unsigned long lastMessage;            // only used for simple IR button switcher for any IR button pressed
byte irProtocolState;      // What our protocol decoder is doing.

void processIRMessage(long message);

ISR(PCINT0_vect)
{
  boolean isPinHigh = digitalRead(IR_PIN);
  long currentTime = micros();
  long microsSinceLastChange = (currentTime - timeOfLastIRPinChange);
  timeOfLastIRPinChange = currentTime;
  
  // Bump up the commit time, since we got a level change.
  timeToCommitMessage = MSG_COMMIT_TIME_MICROSEC + timeOfLastIRPinChange;
  if (!isPinHigh)
  {
     // Going low.
    if (microsSinceLastChange < 700)
    {
      // Space
      message = message << 1;
    }
    if (microsSinceLastChange < 2000)
    {
      // Mark.
      message = message << 1;
      message |= 1;
    }
    else if (microsSinceLastChange < 20000)
    {
      // First LOW.
      message = 0;
      irProtocolState = Building;
    }
  }
  
}
#endif

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= _BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

void setup() {
  // initialize trinket to run at 16MHz
  if (F_CPU == 16000000) clock_prescale_set(clock_div_1);
  
#ifdef USE_BUTTON
  pinMode(BUTTON_PIN, INPUT);
#endif
  
  strip.begin();
  strip.setBrightness(30);
  strip.show(); // Initialize all pixels to 'off'
  
#ifndef USE_BUTTON
  pinMode(IR_PIN, INPUT);
  sbi(GIMSK,PCIE);              // Enable pin change interrupt
  sbi(PCMSK,IR_PIN_INTERRUPT);  // Enable the interrupt for only pin 1.

  lastMessage = 0;
  timeOfLastIRPinChange = micros();
  timeToCommitMessage = -1;
  irProtocolState = Idle;
#endif

  // This is only needed on 5V Arduinos (Uno, Leonardo, etc.).
  // Connect 3.3V to mic AND TO AREF ON ARDUINO and enable this
  // line.  Audio samples are 'cleaner' at 3.3V.
  // COMMENT OUT THIS LINE FOR 3.3V ARDUINOS (FLORA, ETC.):
 // analogReference(EXTERNAL);

  memset(vol, 0, sizeof(vol));
  
  if (!strip.getPixels())
  {
    errorBlinkRedLed();
  }
}



void loop()
{
#ifdef USE_BUTTON
  debounceButton();  // updates mode if button was pressed
#else
  // Check to see if we can commit a message. If so, process it.
  if (micros() >= timeToCommitMessage && irProtocolState == Building)
  {
    long localMessage = message;
    message = 0;
    irProtocolState = Idle;
    if (lastMessage != localMessage)
    {
      lastMessage = localMessage;
      switchMode();
    }
  }
#endif

  switch (mode) {
    case MODE_OFF:
      off();
      break;

    case MODE_VUMETER:
      vumeter();
      break;

#ifdef DOT
    case MODE_DOT_UP:
      runningDotUp();
      break;

    case MODE_DOT_DOWN:
      runningDotDown();
      break;

    case MODE_DOT_ZIGZAG:
      runningDotZigZag();
      break;
#endif

    case MODE_RAINBOW:
      rainbow();
      break;

    case MODE_RAINBOW_CYCLE:
      rainbowCycle();
      break;

    case MODE_WIPE_RED:
      colorWipe(255, 0, 0);
      break;

    case MODE_WIPE_GREEN:
      colorWipe(0, 255, 0);
      break;

    case MODE_WIPE_BLUE:
      colorWipe(0, 0, 255);
      break;

    case MODE_WIPE_YELLOW:
      colorWipe(255, 255, 0);
      break;

    case MODE_WIPE_CYAN:
      colorWipe(0, 255, 255);
      break;

    case MODE_WIPE_MAGENTA:
      colorWipe(255, 0, 255);
      break;
  }
  
}


void off()
{
  if (peak != N_PIXELS) // only once
  {
    peak = N_PIXELS; // move outside
    drawDot();
  }
}

/*
 Error handler - blink first led if we have not enough memory
 */
void errorBlinkRedLed()
{
  Adafruit_NeoPixel ministrip = Adafruit_NeoPixel(1, STRIP_PIN, NEO_GRB + NEO_KHZ800);
  ministrip.begin();
  byte red;
  while (1)
  {
    red = red ? 0 : 50;
    ministrip.setPixelColor(0, red,0,0);
    ministrip.show();
    delay(100);
  }
}

void switchMode()
{
  peak = 0;
  lvl = 10;
  mode++;
  if (mode >= MODE_MAX)
  {
    mode = peak;
  }
}

/*
 Debounce button on pin 1 of trinket.
 Derived from http://www.arduino.cc/en/Tutorial/Debounce
 */
 
#ifdef USE_BUTTON
void debounceButton()
{
  // Some example procedures showing how to display to the pixels:
  // read the state of the switch into a local variable:
  byte reading = digitalRead(BUTTON_PIN);
  
  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH),  and you've waited
  // long enough since the last press to ignore any noise:  

  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState)
  {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }
 
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY)
  {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState)
    {
      buttonState = reading;

      // only toggle the LED if the new button state is HIGH
      if (buttonState == HIGH)
      {
        switchMode();
      }
    }
  }

  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  lastButtonState = reading;
}
#endif

#ifdef DOT
void runningDotUp()
{
  if (millis() - lastTime >= DOT_RUN_MILLIS)
  {
    lastTime = millis();

    drawDot();
 
    if (peak>=LAST_PIXEL_OFFSET)
    {
      peak = 0;
    }
    else
    {
      peak++;
    }
  }
}

void runningDotDown()
{
  if (millis() - lastTime >= DOT_RUN_MILLIS)
  {
    lastTime = millis();

    drawDot();
 
    if (peak <= 0)
    {
      peak = LAST_PIXEL_OFFSET;
    }
    else
    {
      peak--;
    }
  }
}

byte runningDotZigZag()
{
  byte prevpeak = peak;
  if (reverse)
  {
    runningDotDown();
    if (prevpeak != peak && peak == LAST_PIXEL_OFFSET)
    {
      reverse = 0;
      peak = prevpeak;
    }
  }
  else
  {
    runningDotUp();
    if (prevpeak != peak && !peak)
    {
      reverse = 1;
      peak = prevpeak;
    }
  }
}
#endif

void drawDot()
{
  for (int i=0; i<N_PIXELS;i++)
  {
    if (i != peak)
    {
      strip.setPixelColor(i, 0,0,0);
    }
    else
    {
      strip.setPixelColor(i, 255,255,255);
    }
  }
  strip.show();
}

// Fill the dots one after the other with a color
void colorWipe(uint8_t r, uint8_t g, uint8_t b)
{
  if (millis() - lastTime >= DOT_RUN_MILLIS)
  {
    lastTime = millis();

    strip.setPixelColor(peak, r, g, b);
    strip.show();

    if (peak >= LAST_PIXEL_OFFSET)
    {
      peak = 0;
    }
    else
    {
      peak++;
    }
  }
}

void rainbow()
{
  uint16_t i;

  if (millis() - lastTime >= DOT_RUN_MILLIS)
  {
    lastTime = millis();

    if (lvl >= 256)
    {
      lvl = 0;
    }
    else
    {
      lvl++;
    }
    
    for(i=0; i<strip.numPixels(); i++)
    {
      strip.setPixelColor(i, Wheel((i+lvl) & 255));
    }
    strip.show();
  }
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle() {
  uint16_t i;

  if (millis() - lastTime >= DOT_RUN_MILLIS)
  {
    lastTime = millis();

    if (lvl >= 5*256) // 5 cycles of all colors on wheel
    {
      lvl = 0;
    }
    else
    {
      lvl++;
    }
    
    for(i=0; i< strip.numPixels(); i++)
    {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + lvl) & 255));
    }
    strip.show();
  }
}


void vumeter()
{
  uint8_t  i;
  uint16_t minLvl, maxLvl;
  int      n, height;

  n   = analogRead(MIC_PIN);                        // Raw reading from mic 
  n   = abs(n - 512 - DC_OFFSET); // Center on zero
  n   = (n <= NOISE) ? 0 : (n - NOISE);             // Remove noise/hum
  lvl = ((lvl * 7) + n) >> 3;    // "Dampened" reading (else looks twitchy)

  // Calculate bar height based on dynamic min/max levels (fixed point):
  height = TOP * (lvl - minLvlAvg) / (long)(maxLvlAvg - minLvlAvg);

  if(height < 0L)       height = 0;      // Clip output
  else if(height > TOP) height = TOP;
  if(height > peak)     peak   = height; // Keep 'peak' dot at top

#ifdef CENTERED
 // Color pixels based on rainbow gradient
  for(i=0; i<(N_PIXELS/2); i++) {
    if(((N_PIXELS/2)+i) >= height)
    {
      strip.setPixelColor(((N_PIXELS/2) + i),   0,   0, 0);
      strip.setPixelColor(((N_PIXELS/2) - i),   0,   0, 0);
    }
    else
    {
      strip.setPixelColor(((N_PIXELS/2) + i),Wheel(map(((N_PIXELS/2) + i),0,strip.numPixels()-1,30,150)));
      strip.setPixelColor(((N_PIXELS/2) - i),Wheel(map(((N_PIXELS/2) - i),0,strip.numPixels()-1,30,150)));
    }
  }
  
  // Draw peak dot  
  if(peak > 0 && peak <= LAST_PIXEL_OFFSET)
  {
    strip.setPixelColor(((N_PIXELS/2) + peak),255,255,255); // (peak,Wheel(map(peak,0,strip.numPixels()-1,30,150)));
    strip.setPixelColor(((N_PIXELS/2) - peak),255,255,255); // (peak,Wheel(map(peak,0,strip.numPixels()-1,30,150)));
  }
#else
  // Color pixels based on rainbow gradient
  for(i=0; i<N_PIXELS; i++)
  {
    if(i >= height)
    {
      strip.setPixelColor(i,   0,   0, 0);
    }
    else
    {
      strip.setPixelColor(i,Wheel(map(i,0,strip.numPixels()-1,30,150)));
    }
  }

  // Draw peak dot  
  if(peak > 0 && peak <= LAST_PIXEL_OFFSET)
  {
    strip.setPixelColor(peak,255,255,255); // (peak,Wheel(map(peak,0,strip.numPixels()-1,30,150)));
  }
  
#endif  

  // Every few frames, make the peak pixel drop by 1:

  if (millis() - lastTime >= PEAK_FALL_MILLIS)
  {
    lastTime = millis();

    strip.show(); // Update strip

    //fall rate 
    if(peak > 0) peak--;
    }

  vol[volCount] = n;                      // Save sample for dynamic leveling
  if(++volCount >= SAMPLES) volCount = 0; // Advance/rollover sample counter

  // Get volume range of prior frames
  minLvl = maxLvl = vol[0];
  for(i=1; i<SAMPLES; i++)
  {
    if(vol[i] < minLvl)      minLvl = vol[i];
    else if(vol[i] > maxLvl) maxLvl = vol[i];
  }
  // minLvl and maxLvl indicate the volume range over prior frames, used
  // for vertically scaling the output graph (so it looks interesting
  // regardless of volume level).  If they're too close together though
  // (e.g. at very low volume levels) the graph becomes super coarse
  // and 'jumpy'...so keep some minimum distance between them (this
  // also lets the graph go to zero when no sound is playing):
  if((maxLvl - minLvl) < TOP) maxLvl = minLvl + TOP;
  minLvlAvg = (minLvlAvg * 63 + minLvl) >> 6; // Dampen min/max levels
  maxLvlAvg = (maxLvlAvg * 63 + maxLvl) >> 6; // (fake rolling average)
}



// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
   return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
   WheelPos -= 170;
   return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

