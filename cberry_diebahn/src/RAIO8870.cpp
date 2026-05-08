/*#############################################################################
Copyright (C) 2014 Dr. Thomas Glomann

Author:		Dr. Thomas Glomann
                t.glomann@googlemail.com (subject [RAIO])

Version:        0.3 (initial release)

last update:    2014-05-06
*##############################################################################
Description:

Here, high-level drawing and control functions are defined for
convenient use of the RAIO graphics controller, supporting
e.g. 2-layers in 4k-Mode, usefull for double-buffered output.

The class RAIO8870 defines the registers and access functions of
the RAIO8870 graphics controller.

The controller communicates with the Rasperry Pis via the SPI
interface. The interface communication is handled by the TFT class.

*##############################################################################
This Code is derived from the original work by
Copyright (C) 2013 admatec GmbH
Author: 	Hagen Ploog, Kai Gillmann, Timo Pfander
Date: 		2013-11-22   	    last update: 2014-02-28
*##############################################################################

License:

This file is part of the C-Berry LCD C++ driver library.

The C-Berry LCD C++ driver library is free software: you can redistribute
it and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version.

The C-Berry LCD C++ driver library is distributed in the hope that it will
be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the C-Berry LCD C++ driver library.
If not, see <http://www.gnu.org/licenses/>.
*############################################################################*/

#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <cmath>
#include "header/RAIO8870.h"

window::window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
  left = x1;
  top = y1;
  right = x2;
  bottom = y2;
  width = x2 - x1;
  height = y2 - y1;
}

RAIO8870::RAIO8870(ColorMode CM, int Width, int Height) : colorMode(CM), width(Width), height(Height), pixels(Width*Height)
{
 // std::cerr << "Constructur RAIO8870" << std::endl;
  // initialilze the display
  PLL_Initial_Flag = 0;
  state = Disconnected;
  init(); // initialize LCD and reset to default settings
}



RAIO8870::~RAIO8870()
{
  if (state == Connected) {
    setBacklightValue(0);
    setRegister( PWRR, 0x00 ); // turn off power
    bcm2835_close();  // close the interface
  }
}

// write command to a register
// ----------------------------------------------------------
void RAIO8870::setRegister( uint8_t reg, uint8_t value )
{
  if (state == Disconnected)
    return;
  tft.RegWrite( (uint16_t)reg );
  tft.DataWrite( (uint16_t)value );
}

// initialization of RAIO8870
// ----------------------------------------------------------
void RAIO8870::init()
{
  if (state == Disconnected) {
    if (bcm2835_init() == 1) // initialize the SPI interface
      state = Connected;
    else
      state = Disconnected;
  }
  reset();
}

void RAIO8870::reset()
{
  if (state == Connected) {
    tft.init_board();
    tft.hard_reset();

    // *************** PLL settings (System Clock)s
    if ( !PLL_Initial_Flag )				// wait until PLL is ready
    {
      setRegister( PLLC1, 0x07 );    // set sys_clk
      bcm2835_delayMicroseconds( 200 );
      setRegister( PLLC2, 0x03 );    // set sys_clk
      bcm2835_delayMicroseconds( 200 );
      PLL_Initial_Flag = 1;               // set Flag to avoid repeated PLL init
    }
    // software reset of display
    setRegister( PWRR, 0x01 );     // Raio software reset ( bit 0 ) set
    setRegister( PWRR, 0x00 );     // Raio software reset ( bit 0 ) set to 0
    delay( 100 );
  }

  // init LCD settings  --> given by constructor arguments.
  setHorizontal(); // e.g. 320 px
  setVertical(); // 240 px
  setColorMode(); // 65 or 4k Mode

  // *************** miscellaneous settings, --> default values
  setRegister( PCLK, 0x00 );  // PCLK fetch data on rising edge
  setRegister( IODR, 0x07 );  // ??? some buffer settings of GPIO pins ???
  setRegister( PWRR, 0x80 ); // finally, turn on power !!

  // finally restore default settings;
  resetDefaults();
}

void RAIO8870::resetDefaults() // reset to default settings for e.g. textmode
{
  setBacklightValue(50); // Backlight brightness (0 .. 255)

  setFontSizeFactor(1); // --> fontSizeH = 8;  fontSizeV = 16;  px
  setFontLineDistance(0); // --> fontLineDistance = 0; px gap between text lines
  setTabSize(40); // --> tabSize = 40; px
  // the following line is equivalent with the following next "setCursor..." Lines
  //  textModeSettings = (TextMode | TextCursorNoAutoIncrease | TextCursorVisible | TextCursorBlinking); // define Text mode with specific Cursor settings
  textModeSettings = (TextMode);
  setCursorVisible(true); // enable cursor blinking, -> sets Flag Bit TextCursorBlinking;
  setCursorAutoIncrease(false); // enable cursor blinking, -> sets Flag Bit TextCursorBlinking;
  setPeriodicBoundary(true); // enable periodic boundary conditions, beyond last line will overide first line
  setCursorBlinking(true); // enable cursor blinking, -> sets Flag Bit TextCursorBlinking;
  setCursorBlinkingTime(15); // --> cursor blinking time of 15 frames, nice blinking time
  setDisplayMode(textModeSettings);

  // default colors
  setTextBackgroundColor( BLACK ); // Background Colors
  BTE_Background_Color_Red(0);
  BTE_Background_Color_Green(0);
  BTE_Background_Color_Blue(0);

  setTextWindow(0, 0, width-1, height-1); // Text Window is Full Screen
  setActiveWindow(0, 0, width-1, height-1);// set Active Window within RAIO controller to full screen
  clear(Screen); // memory clear with background color, full window, clears with text bg color
}


// set PWM value for backlight
// ----------------------------------------------------------
void RAIO8870::setBacklightValue( uint8_t BL_value )
{
  setRegister( P1CR, 0x88 ); 	 // Enable PWM1 output devider 256
  setRegister( P1DCR, BL_value ); // -> BL_vaue = 0 (0% PWM) - 255 (100% PWM)
}

void RAIO8870::setHorizontal()
{
  // 0x27+1 * 8 = 320 pixel
  setRegister( HDWR , (width / 8) - 1 );
  setRegister( HNDFTR, 0x02 ); // Horizontal Non-Display Period Fine Tuning

  // HNDR , Horizontal Non-Display Period Bit[4:0]
  // Horizontal Non-Display Period (pixels) = (HNDR + 1)*8
  setRegister( HNDR, 0x03 );                            //       0x06
  setRegister( HSTR, 0x04 );   //HSTR , HSYNC Start Position[4:0], HSYNC Start Position(PCLK) = (HSTR + 1)*8     0x02

  // HPWR , HSYNC Polarity ,The period width of HSYNC.
  // 1xxxxxxx activ high 0xxxxxxx activ low
  // HSYNC Width [4:0] HSYNC Pulse width
  // (PCLK) = (HPWR + 1)*8
  setRegister( HPWR, 0x03 );   // 0x00
}

void RAIO8870::setVertical()
{
  // 0x0EF +1 = 240 pixel
  setRegister(  VDHR0 , ( (height-1) & 0xFF ) );
  setRegister(  VDHR1 , ( (height-1) >> 8)    );

  // VNDR0 , Vertical Non-Display Period Bit [7:0]
  // Vertical Non-Display area = (VNDR + 1)
  // VNDR1 , Vertical Non-Display Period Bit [8]
  // Vertical Non-Display area = (VNDR + 1)
  setRegister( VNDR0, 0x10 );
  setRegister( VNDR1, 0x00 );

  // VPWR , VSYNC Polarity ,VSYNC Pulse Width[6:0]
  // VSYNC , Pulse Width(PCLK) = (VPWR + 1)
  setRegister( VPWR, 0x00 );
}

void RAIO8870::setColorMode()
{
  if (colorMode == CM_65K) {
   // std::cout << "initializing 65K color mode" << std::endl;
    // System Configuration Register
    // Bits: 0000 1010: ->
    // parallel data out
    // no external memory
    // 8bit memory data bus
    // 16bpp 65K color
    // 16bit MCU-interface (data)
    setRegister( SYSR, 0x0A );
    // Layer settings
    setRegister( DPCR, 0x00 ); // one layer, scan directions normal, no rotation
  }
  else if (colorMode == CM_4K) {
    std::cout << "initializing 4K color mode" << std::endl;
    // System Configuration Register
    // Bits: 0000 0110: ->
    // digital TFT
    // parallel data out
    // no external memory
    // 8bit memory data bus
    // 12bpp 4K color
    // 16bit MCU-interface (data)
    setRegister( SYSR, 0x06 );
    // Layer settings
    setRegister( DPCR, 0x80 ); // two layers, scan directions normal, no rotation
    activeLayer = 0; // either 1 or 0 (LSB)
    setRegister( MWCR1, activeLayer); // write to active layer
    setRegister( LTPR0, activeLayer); // show only active layer (usefull for double-buffering)
    layerTransparancySettings = NoTransparancy;
    setRegister( LTPR1, layerTransparancySettings ); // Layer Transparancy, e.g. 0x44 -> 50% display, both layers
  }
  else {
    std::cerr << "undefined colorMode, defaulting to 65K mode\n" << std::endl;
    setRegister( SYSR, 0x0A );
    setRegister( DPCR, 0x00 ); // one layer, scan directions normal, no rotation
  }
}


// set coordinates for active window
// ----------------------------------------------------------
void RAIO8870::setActiveWindow( uint16_t XL, uint16_t YT, uint16_t XR , uint16_t YB)
{
  // check if withing physical display boundaries
  XL = XL%width;
  XR = XR%width;
  YT = YT%height;
  YB = YB%height;

  activeWindow = window(XL, YT, XR, YB);

  union my_union number;
  //setting active window X
  number.value = XL;
  setRegister( HSAW0, number.split.low );
  setRegister( HSAW1, number.split.high );

  number.value = XR;
  setRegister( HEAW0, number.split.low );
  setRegister( HEAW1, number.split.high );

  //setting active window Y
  number.value = YT;
  setRegister( VSAW0, number.split.low );
  setRegister( VSAW1, number.split.high );

  number.value = YB;
  setRegister( VEAW0, number.split.low );
  setRegister( VEAW1, number.split.high );
}

void RAIO8870::setTextWindow( uint16_t XL, uint16_t YT, uint16_t XR , uint16_t YB)
{
  // check if withing physical display boundaries
  XL = XL%width;
  XR = XR%width;
  YT = YT%height;
  YB = YB%height;

  textWindow = window(XL, YT, XR, YB);
  setCursor(); // automatically also reset the cursor to top-left of active window
}

void RAIO8870::setTextWindow( uint16_t XL, uint16_t YT, uint16_t XR , uint16_t YB, uint8_t color)
{
  // check if withing physical display boundaries
  XL = XL%width;
  XR = XR%width;
  YT = YT%height;
  YB = YB%height;

  textWindow = window(XL, YT, XR, YB);
  setCursor(); // automatically also reset the cursor to top-left of active window

  drawRect(XL-1, YT-1, XR+1, YB+1, color);
}

// set coordinates for scroll window
// ----------------------------------------------------------
void RAIO8870::setScrollWindow( uint16_t XL, uint16_t YT, uint16_t XR , uint16_t YB )
{
  // check if withing physical display boundaries
  XL = XL%width;
  XR = XR%width;
  YT = YT%height;
  YB = YB%height;

  scrollWindow = window(XL, YT, XR, YB);
  setCursor(); // automatically also reset the cursor to top-left of active window
  union my_union number;

  //setting scroll window X
  number.value = XL;
  setRegister( HSSW0, number.split.low );
  setRegister( HSSW1, number.split.high );

  number.value = XR;
  setRegister( HESW0, number.split.low );
  setRegister( HESW1, number.split.high );


  //setting scroll window Y
  number.value = YT;
  setRegister( VSSW0, number.split.low );
  setRegister( VSSW1, number.split.high );

  number.value = YB;
  setRegister( VESW0, number.split.low );
  setRegister( VESW1, number.split.high );
}


void RAIO8870::setHScrollOffset(uint16_t offset)
{
  union my_union number;

  //setting H scroll offset
  number.value = offset;
  setRegister( HOFS0, number.split.low );
  setRegister( HOFS1, number.split.high );
}

void RAIO8870::setVScrollOffset(uint16_t offset)
{
  union my_union number;

  //setting V scroll offset
  number.value = offset;
  setRegister( VOFS0, number.split.low );
  setRegister( VOFS1, number.split.high );
}


void RAIO8870::setDisplayMode(uint8_t newMode)
{
  setRegister( MWCR0, newMode );
}


// set mode for BET (Block Transfer Engine)
// ----------------------------------------------------------
void RAIO8870::BTE_mode( uint8_t bte_operation, uint8_t rop_function )
{
  setRegister( BECR1, bte_operation | (rop_function<<4) );
}


// set colors
// ----------------------------------------------------------
void RAIO8870::setTextBackgroundColor( uint8_t color )
{
  setRegister( TBCR, color );
}
void RAIO8870::setTextForegroundColor( uint8_t color)
{
  setRegister( TFCR, color);
}
// BTE Background Colors, Reg 0x60, 0x61, 0x62 define BTE background color
void RAIO8870::BTE_Background_Color_Red( uint8_t color )
{
  setRegister( BGCR0, color ); // BGCR0=red[4:0] (65k), red[4:1] (4k)
}
void RAIO8870::BTE_Background_Color_Green( uint8_t color )
{
  setRegister( BGCR1, color ); // BGCR1=green[5:0] (65k), red[5:2] (4k)
}
void RAIO8870::BTE_Background_Color_Blue( uint8_t color )
{
  setRegister( BGCR2, color ); // BGCR2=blue[4:0] (65k), red[4:1] (4k)
}
// BTE Foreground Colors, Reg 0x63, 0x64, 0x65 define BTE foreground color
void RAIO8870::BTE_Foreground_Color_Red( uint8_t color )
{
  setRegister( FGCR0, color ); // FGCR0=red[4:0] (65k), red[4:1] (4k)
}
void RAIO8870::BTE_Foreground_Color_Green( uint8_t color )
{
  setRegister( FGCR1, color ); // FGCR1=green[5:0] (65k), red[5:2] (4k)
}
void RAIO8870::BTE_Foreground_Color_Blue( uint8_t color )
{
  setRegister( FGCR2, color ); // FGCR2=blue[4:0] (65k), red[4:1] (4k)
}



// clear screen
// ----------------------------------------------------------
void RAIO8870::clear(ClearMode cm)
{
  setRegister( MCLR , cm ); // clear screen with CM bg color
  if (state == Disconnected)
    return;
  tft.wait_for_raio();
}

void RAIO8870::clearAll(ClearMode cm)
{
  setTextWindow(0, 0, width-1, height-1); // reset active window to full screen
  setCursor(); // reset cursor to active windows top-left
  clear(cm);
}

void RAIO8870::swapLayerWrite()
{
  if (activeLayer == 0)
    setRegister(MWCR1,1);
  else
    setRegister(MWCR1,0);
}

void RAIO8870::swapLayer()
{
  if (activeLayer == 0) {
    setRegister(LTPR0,1);
    activeLayer = 1;
  }
  else {
    setRegister(LTPR0,0);
    activeLayer = 0;
  }
}

void RAIO8870::setCursorVisible(bool yes)
{
  if (yes)
    textModeSettings = (textModeSettings | TextCursorVisible);
  else
    textModeSettings = (textModeSettings & (~TextCursorVisible));
  setDisplayMode(textModeSettings);
}

void RAIO8870::setCursorAutoIncrease(bool yes)
{
  if (yes) {
    textModeSettings = (textModeSettings & (~TextCursorNoAutoIncrease));
    cursorAutoIncrease = 0x01;
  }
  else {
    textModeSettings = (textModeSettings | TextCursorNoAutoIncrease);
    cursorAutoIncrease = 0x00;
  }
  setDisplayMode(textModeSettings);
}

void RAIO8870::setCursorBlinking(bool yes)
{
  if (yes)
    textModeSettings = (textModeSettings | TextCursorBlinking);
  else
    textModeSettings = (textModeSettings & (~TextCursorBlinking));
  setDisplayMode(textModeSettings);
}

void RAIO8870::setCursorBlinkingTime(uint8_t t)
{
  setRegister( BTCR, t);
  setDisplayMode(textModeSettings);
}

// set (text) cursor positions to relative position within display boundaries
void RAIO8870::setCursorAbsolute(uint16_t pos_x, uint16_t pos_y)
{
  cursorX = pos_x%width;
  cursorY = pos_y%height;

  //  std::cerr << "setting absolute cursor position to (X,Y): "<< cursorX << "," << cursorY << std::endl;

  union my_union number;

  number.value = cursorX;
  setRegister( CURH0, number.split.low );
  setRegister( CURH1, number.split.high );

  number.value = cursorY;
  setRegister( CURV0, number.split.low );
  setRegister( CURV1, number.split.high );
}

// set (text) cursor positions to relative position within display boundaries
void RAIO8870::setCursor(uint16_t newX, uint16_t newY)
{
  // Here is the algorithm to preserve all whitespaces, newlines, and so on,
  // example: if a line has a capacity of 40 characters, and cursor is at the last possible poisiton (40th character)
  //   and we write 3 whitespaces, we will generate a new line and add 2 white spaces on the next line
  uint8_t numberNewLines = 1;
  // do proper action if we hit the "right" window boundary
  if (newX > (textWindow.right)) { // if new character would be COMPLETELY outside of the right edge of the window
    //    std::cerr << "crossing right window boundary at " << newX <<"," << newY << " --> new line" << std::endl;
    numberNewLines += (newX-textWindow.right) / (textWindow.width); // calc. number of newlines to add
    newX = textWindow.left /*+ ( ( (newX%(textWindow.width+1)) / fontSizeH )*fontSizeH )*/; // set to the left edge of window, shifted by amount of "overwrite" from the previous line in steps of 1 character size
    newY += numberNewLines * (fontSizeV + fontLineDistance); // generate a suiteable number of newlines
    //    std::cerr << " --> new cursor position: " << newX <<"," << newY << std::endl;
  }
  // now do the proper action if we hit the "bottom" window boundary
  if (newY > textWindow.bottom) { // if new character would be COMPLETELY outside of the bottom edge of the window
    //    std::cerr << "crossing bottom window boundary at " << newX <<"," << newY << " --> depends on periodicity" << std::endl;
    if (periodicBoundary) { // periodic boundaries in Y direction -> text beyond bottom will appear at first line
      newY = textWindow.top/* + ( ( (newY%(textWindow.height+1)) / fontSizeV )*fontSizeV )*/; // periodic boundary condition
    }
    else // NO periodic boundaries in Y direction -> overwrite the bottom line over and over again
      newY = textWindow.bottom + 1 - fontSizeV;
  }
  setCursorAbsolute(newX, newY);
}

// print text
// ----------------------------------------------------------
void RAIO8870::drawText( std::string str,  uint16_t pos_x, uint16_t pos_y, uint8_t FG_color, uint8_t BG_color )
{
  setCursor(pos_x, pos_y);
  addText(str.c_str(), FG_color, BG_color);
}

void RAIO8870::drawText(const char * str,  uint16_t pos_x, uint16_t pos_y,uint8_t FG_color, uint8_t BG_color )
{
  setCursor(pos_x, pos_y);
  addText(str, FG_color, BG_color);
}

void RAIO8870::drawText(char c,  uint16_t pos_x, uint16_t pos_y,uint8_t FG_color, uint8_t BG_color )
{
  setCursor(pos_x, pos_y);
  addText(c, FG_color, BG_color);
}

void RAIO8870::addText( std::string str, uint8_t FG_color, uint8_t BG_color )
{
  addText(str.c_str(), FG_color, BG_color);
}

void RAIO8870::addText(char c, uint8_t FG_color, uint8_t BG_color )
{
  char str[2];
  str[0] = c;
  str[1] = '\0';
  addText(str, FG_color, BG_color);
}

void RAIO8870::addText(const char * str, uint8_t FG_color, uint8_t BG_color ) // the ACTUAL TEXT ROUTINE
{
  setDisplayMode(textModeSettings); // restore TextMode Settings (Cursor Mode)
  setTextBackgroundColor( BG_color );   // set colors
  setTextForegroundColor( FG_color );   // set colors

  // write text to display, byte-by-byte, filter special characters '\n' and '\t'
  while ( *str != '\0' )
  {
    // follow cursor positions
    uint16_t newX = cursorX;
    uint16_t newY = cursorY;
    // act on ascii control characters
    if (*str == '\n') {       // "new line"
      //      std::cerr << "newline detected" << std::endl;
      newY += fontSizeV + fontLineDistance;
      newX = textWindow.left; // go the left of next line on active window
    }
    else if (*str == '\t') {  // "tab"
      //      std::cerr << "tab detected" << std::endl;
      newX = tabSize * ((cursorX/tabSize)+1); // allign X at multiple of tabSize (e.g. 40, 80, 120, ...)
    }
    else {   // print any other ascii character
      writeCharacter(*str); // output the character to memory
      newX += fontSizeH; // increase cursor position
    }
    ++str; // go to next character
    if (!cursorAutoIncrease) // if manual cursor control, set cursor to the next position
      setCursor(newX, newY);
  }
}

void RAIO8870::writeCharacter(uint8_t c)
{
  if (state == Connected) {
    tft.RegWrite( (uint16_t)MRWC );
    tft.DataWrite( (uint16_t)c );
    tft.wait_for_raio();
  }
}

void RAIO8870::present(std::string s,  bool clear)
{
  if (clear)
    clearAll();
  int16_t spaceFirst = 0;
  int16_t spaceNext = s.find_first_of(' ', spaceFirst);
  while(spaceNext!=std::string::npos) {
    addText(s.substr(spaceFirst, spaceNext-spaceFirst+1), GREEN);
    spaceFirst = spaceNext+1;
    spaceNext = s.find_first_of(' ', spaceFirst);
    delay(500);
  }
  addText(s.substr(spaceFirst), GREEN);
  delay(5000);
}

// set font size
// ----------------------------------------------------------
void RAIO8870::setFontSizeFactor(uint8_t size)
{
  uint8_t factor = 1;
  if (size == 1)
    factor = 1;
  else if (size == 2)
    factor = 2;
  else if (size == 3)
    factor = 3;
  else if (size == 4)
    factor = 4;
  else
    std::cerr << "ERROR: illegal fontSizeFactor " << size <<"! only factor from 1 to 4 possible!\n" << std::endl;

  fontSizeH = 8 * factor;
  fontSizeV = 16 * factor;

  if (state == Disconnected)
    return;
  uint8_t sizeH = (factor -1) << 2;
  uint8_t sizeV = (factor -1);
  setRegister ( FNCR1, sizeH | sizeV );
}

// set coordinates for drawing line and square
// ----------------------------------------------------------
void RAIO8870::setCoordinates(uint16_t X1, uint16_t Y1 ,uint16_t X2 ,uint16_t Y2 )
{
  // check if withing physical display boundaries
  X1 = X1%width;
  X2 = X2%width;
  Y1 = Y1%height;
  Y2 = Y2%height;

  union my_union number;

  number.value = X1;
  setRegister( DLHSR0, number.split.low );
  setRegister( DLHSR1, number.split.high );

  number.value = Y1;
  setRegister( DLVSR0, number.split.low  );
  setRegister( DLVSR1, number.split.high );

  number.value = X2;
  setRegister( DLHER0, number.split.low );
  setRegister( DLHER1, number.split.high );

  number.value = Y2;
  setRegister( DLVER0, number.split.low );
  setRegister( DLVER1, number.split.high );
}

// set coordinates for drawing circle
// ----------------------------------------------------------
void RAIO8870::setCoordinates(uint16_t X1, uint16_t Y1 ,uint8_t rad )
{
  // check if withing physical display boundaries
  X1 = X1%width;
  Y1 = Y1%height;
  rad = rad%height;

  union my_union number;

  number.value = X1;
  setRegister( DCHR0, number.split.low );
  setRegister( DCHR1, number.split.high );

  number.value = Y1;
  setRegister( DCVR0, number.split.low  );
  setRegister( DCVR1, number.split.high );

  setRegister( DCRR, rad );
}


// set draw mode
// ----------------------------------------------------------
void RAIO8870::startDrawing( int16_t whattodraw )
{
  // set graphics mode
  setDisplayMode(GraphicsMode);

  switch( whattodraw ) // -> see DRAW_MODES
  {
    case CIRCLE_NONFILL:    {setRegister( DCR,  0x40 ); break;}  // 0100 0000
    case CIRCLE_FILL:       {setRegister( DCR,  0x60 ); break;}  // 0110 0000
    case SQUARE_NONFILL:    {setRegister( DCR,  0x90 ); break;}  // 1001 0000
    case SQUARE_FILL:       {setRegister( DCR,  0xB0 ); break;}  // 1011 0000
    case LINE:              {setRegister( DCR,  0x80 ); break;}  // 1000 0000
    default: break;
  }
  if (state == Disconnected)
    return;
  tft.wait_for_raio();
  // stop drawing
  //  setRegister( DCR,  0x00 );
}


// draw some basic geometrical forms
// ----------------------------------------------------------
void RAIO8870::drawLine(uint16_t X1, uint16_t Y1 ,uint16_t X2 ,uint16_t Y2, uint8_t fgColor)
{
  setCoordinates( X1, Y1, X2, Y2 );
  setTextForegroundColor(fgColor);
  startDrawing(LINE);
}

void RAIO8870::drawRect( uint16_t X1, uint16_t Y1 ,uint16_t X2 ,uint16_t Y2, uint8_t fgColor, bool filled )
{
  setCoordinates( X1, Y1, X2, Y2 );
  setTextForegroundColor(fgColor);
  if (filled) // execute drawing
    startDrawing(SQUARE_FILL);
  else
    startDrawing(SQUARE_NONFILL);
}

void RAIO8870::drawCircle(uint16_t X1, uint16_t Y1 , uint8_t rad, uint8_t fgColor, bool filled )
{
  setCoordinates( X1, Y1, rad );
  setTextForegroundColor(fgColor);
  if (filled) // execute drawing
    startDrawing(CIRCLE_FILL);
  else
    startDrawing( CIRCLE_NONFILL );
}

// show the BMP picture on the TFT screen
// ----------------------------------------------------------
void RAIO8870::drawBMP(char const *filename)
{
  // load the BMP into memory first
  uint16_t *picture = new uint16_t[pixels];         // local bmp array
  if (!loadBMP( filename, &picture[pixels-1])) // if loading is successfull
    return;
  // set graphic mode
  setDisplayMode(GraphicsMode);
  if (state == Disconnected)
    return;
  tft.RegWrite( MRWC );
  tft.DataMultiWrite(picture, pixels);
  tft.wait_for_raio();
  delete []picture;
}


// store BMP files in memory
// ----------------------------------------------------------
bool RAIO8870::loadBMP(char const *file_name, uint16_t *picture_pointer)
{
  FILE *my_file;

  //	bmp_header_t bmp_header;
  uint8_t bmp_header_buffer[54];
  uint8_t *bmp_line_buffer = new uint8_t[width*3];

  uint16_t bfType;
  uint32_t bfOffBits;

  uint32_t biSize;

  int32_t biWidth;
  int32_t biHeight;
  uint16_t biBitCount;

  uint32_t y,x;

  uint8_t red, green, blue;
  uint16_t color;


  // check for file
  //printf( "Opening input file... " );
  if( ( my_file = fopen( file_name, "rb" ) ) == NULL )
  {
    printf( "ERROR: Could not open input file for reading.\n" );
    return(false);
  }
  //printf( "OK\n" );


  // read header
  fread( &bmp_header_buffer, 1, 54, my_file );


  // check for "BM"
  //printf( "Checking magic number... " );
  bfType = bmp_header_buffer[1];
  bfType = (bfType << 8) | bmp_header_buffer[0];
  if( bfType != 0x4D42)
  {
    printf( "ERROR: Not a bitmap file.\n" );
    fclose( my_file );
    return( false );
  }
  //printf( "OK\n" );


  //printf( "Checking header size... " );
  biSize = bmp_header_buffer[17];
  biSize = (biSize << 8) | bmp_header_buffer[16];
  biSize = (biSize << 8) | bmp_header_buffer[15];
  biSize = (biSize << 8) | bmp_header_buffer[14];
  //printf( "%d ", biSize);
  if( biSize != 40 )
  {
    printf( "ERROR: Not Windows V3\n" );
    fclose( my_file );
    return( false );
  }
  //printf( "OK\n" );


  //printf( "Checking dimensions... " );
  biWidth = bmp_header_buffer[21];
  biWidth = (biWidth << 8) | bmp_header_buffer[20];
  biWidth = (biWidth << 8) | bmp_header_buffer[19];
  biWidth = (biWidth << 8) | bmp_header_buffer[18];

  biHeight = bmp_header_buffer[25];
  biHeight = (biHeight << 8) | bmp_header_buffer[24];
  biHeight = (biHeight << 8) | bmp_header_buffer[23];
  biHeight = (biHeight << 8) | bmp_header_buffer[22];

  biBitCount = bmp_header_buffer[29];
  biBitCount = (biBitCount << 8) | bmp_header_buffer[28];

  //printf( "%dx%dx%dbbp. ", biWidth, biHeight, biBitCount );

  if( (biWidth != width) || (biHeight != height) || (biBitCount != 24) )
  {
    printf( "ERROR. %dx%dx%d required.\n", width, height, 24 );
    fclose( my_file );
    return( false );
  }
  //printf( "OK\n\n" );


  bfOffBits = bmp_header_buffer[13];
  bfOffBits = (bfOffBits << 8) | bmp_header_buffer[12];
  bfOffBits = (bfOffBits << 8) | bmp_header_buffer[11];
  bfOffBits = (bfOffBits << 8) | bmp_header_buffer[10];

  //printf( "biOffBits = %d\n", bfOffBits );


  fseek( my_file, bfOffBits, SEEK_SET );
  //printf( "Filling picture buffer... \n" );


  for (y=height; y>0; y--)
  {
    fread( &bmp_line_buffer[0], sizeof(bmp_line_buffer), 1, my_file );
    for (x=width; x>0; x--)
    {

      blue =  bmp_line_buffer[(x-1)*3 +0];
      green = bmp_line_buffer[(x-1)*3 +1];
      red =   bmp_line_buffer[(x-1)*3 +2];

      if (colorMode == CM_65K) {
        color = (red >> 3);
        color = color << 6;
        color = color | (green >> 2);
        color = color << 5;
        color = color | (blue >> 3);
      }
      else if (colorMode == CM_4K) {
        color = ( red >> 4 );
        color = color << 4;
        color = color | ( green >> 4);
        color = color << 4;
        color = color | (blue >> 4);
      }
      else
        color = x%256;

      *picture_pointer = color;
      picture_pointer--;
    }
  }
  fclose( my_file );
  delete [] bmp_line_buffer;
  return ( true );
}
