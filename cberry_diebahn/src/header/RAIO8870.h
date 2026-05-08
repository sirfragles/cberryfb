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

#ifndef RAIO8870_H
#define RAIO8870_H

#include <stdint.h>
#include <string>

extern "C" {
#include <bcm2835.h>
}

// define pins on connector P1 (Rev 2 Boards) --> defined by bcm2835 library (enum)
#define MOSI      RPI_V2_GPIO_P1_19
#define MISO      RPI_V2_GPIO_P1_21
#define SCLK      RPI_V2_GPIO_P1_23
#define OE        RPI_V2_GPIO_P1_11
#define SPI_CE1   RPI_V2_GPIO_P1_26
#define RAIO_RS   RPI_V2_GPIO_P1_12
#define RAIO_RST  RPI_V2_GPIO_P1_22
#define RAIO_CS   RPI_V2_GPIO_P1_24
#define RAIO_WR   RPI_V2_GPIO_P1_18
#define RAIO_RD   RPI_V2_GPIO_P1_16
#define RAIO_WAIT RPI_V2_GPIO_P1_15
#define RAIO_INT  RPI_V2_GPIO_P1_13
#define RAIO_WRpin 24


/******************************************************************************
// convenience function for splitting a 16-Bit integer into high/low byte
******************************************************************************/
union my_union
{
  uint32_t value;  // why 32 Bits ??
  struct 	{
    unsigned char low;
    unsigned char high;
  } split;
};

class window
{
public:
  window(uint16_t x1=0, uint16_t y1=0, uint16_t x2=319, uint16_t y2=239);
  uint16_t left, right, top, bottom, width, height;
};

/******************************************************************************
/ class TFT, communication with the raspberry pi via SPI
******************************************************************************/
class TFT
{
public:
  void init_board( void ); // initialization of GPIO and SPI
  void hard_reset( void ); // hard reset of the graphic controller and the tft
  void wait_for_raio ( void ); // wait during raio is busy
  void SPI_data_out ( uint16_t data );
  void RegWrite( uint16_t reg ); // write byte to register
  void DataWrite( uint16_t data ); // write byte to tft
  void DataMultiWrite( uint16_t *data, uint32_t count ); // write 'count'-bytes to tft
};


/******************************************************************************
/ global enums   -->  add to a namespace later ...
******************************************************************************/
enum ColorMode {CM_65K, CM_4K};
enum State {Disconnected, Connected};
enum TextColor { RED = 0xE0, BLUE = 0x03, GREEN = 0x1C, BLACK = 0x00, WHITE = 0xFF, GREY = 0xBB, CYAN = 0x1F, YELLOW = 0xFC, MAGENTA = 0xE3, DARK_GREEN = 0x0C }; // Text Mode colors (256 colors: "RRRGGGBB")
enum DisplayMode {TextMode = 0x80, GraphicsMode = 0x00,TextCursorVisible = 0x40, TextCursorBlinking = 0x20, TextCursorNoAutoIncrease = 0x03}; // Display Modes -- Register MWCR0
enum LayerMode {OneLayer = 0x00, TwoLayer = 0x80, Layer1Only = 0x00, Layer2Only = 0x01, Overlay = 0x02, Transparent = 0x03, OR = 0x04, AND = 0x05, Layer1Scroll = 0x40, Layer2Scroll = 0x80, LayerBothScroll = 0x00 }; // Layer Modes -- Register DPCR, LTPR0
enum LayerTransparancy {NoTransparancy, Layer1Half = 0x04, Layer1Quarter = 0x06, Layer2Half = 0x40, Layer2Quarter = 0x60, }; // Layer Transp Modes -- Register LTPR1, Bits(0-3) -> Layer 1, Bits(4-7) -> Layer 2
enum ClearMode {Screen = 0x81, textWindow = 0xC1, ScreenBTE = 80, textWindowBTE = 0xC0}; // LSB: 1=Text-BG-color, 0=BTE-BG-color
enum ROP { ROP_SOURCE = 0xC }; // ROP functions
enum BTE { BTE_MOVE_POSITIVE = 0x02, BTE_SOLID_FILL = 0x0C }; // BTE operation functions



/******************************************************************************
/ The RAIO Driver,     provide an API documentation later...
******************************************************************************/

class RAIO8870
{
public:
  explicit RAIO8870(ColorMode CM=CM_4K, int Width=320, int Height=240);
  ~RAIO8870();
  // initialization and settings of RAIO8870
  void init();
  void reset();
  void resetDefaults();
  void setBacklightValue(uint8_t BL_value); // set PWM value for backlight -> 0 (0% PWM) - 255 (100% PWM)
  void clear(ClearMode cm=Screen); // clear memory
  void clearAll(ClearMode cm=Screen); // clear memory, reset textWindow, resetCursor
  
  // gets for internal display state
  int getWidth() {return width;};
  int getHeight() {return height;};
  int getPixels() {return pixels;};
  uint8_t getColorMode() {return colorMode;};
  uint8_t getState() {return state;};
  uint8_t getTextModeSettings() {return textModeSettings;};
  
  // Window and Layer Control functions
  void setActiveWindow(uint16_t XL, uint16_t YT, uint16_t XR , uint16_t YB); // set coordinates for active window, NO DRAWING OUTSIDE OF ACTIVE WINDOW !!!
  void setTextWindow(uint16_t XL, uint16_t YT, uint16_t XR , uint16_t YB); // set coordinates for active Text window
  void setTextWindow( uint16_t XL, uint16_t YT, uint16_t XR , uint16_t YB, uint8_t color);
  void setScrollWindow(uint16_t XL, uint16_t YT, uint16_t XR , uint16_t YB); // set coordinates for scroll window
  void setHScrollOffset(uint16_t offset);
  void setVScrollOffset(uint16_t offset);
  // only used in 2 layer configuration
  void swapLayerWrite();
  void swapLayer();
  
  // Cursor Settings
  void setCursor() {setCursor(textWindow.left, textWindow.top);}; // sets curosr to active windows top-left corner
  void setCursor(uint16_t pos_x, uint16_t pos_y); // sets cursor to arbitrary position
  void setCursorAbsolute(uint16_t pos_x, uint16_t pos_y);
  uint16_t getCursorX() {return cursorX;};
  uint16_t getCursorY() {return cursorY;};
  void setCursorVisible(bool b=true); // enable cursor blinking, -> sets Flag Bit TextCursorBlinking;
  void setCursorAutoIncrease(bool b=false); // enable cursor blinking, -> sets Flag Bit TextCursorBlinking;
  void setCursorBlinking(bool b=true); // enable cursor blinking, -> sets Flag Bit TextCursorBlinking;
  void setCursorBlinkingTime(uint8_t t=15);  // cursor blinking time, 15 frames
  void setPeriodicBoundary(bool b = true) {periodicBoundary = b;};
  void setFontSizeFactor(uint8_t size = 1 ); // set font size scaling factor (1...4)
  void setFontLineDistance(uint8_t size = 0 ) {fontLineDistance = size;}; // set distance between lines
  void setTabSize(uint16_t t = 5) {tabSize = t*fontSizeH;}; // sets the size in number of Characters
  void setTabSizePixels(uint16_t t = 40) {tabSize = t;}; // sets the size in px
  void setTextBackgroundColor(uint8_t color);
  void setTextForegroundColor(uint8_t color);
  
  // high-level text functions
  void drawText(std::string str, uint16_t pos_x, uint16_t pos_y, uint8_t FG_color=WHITE, uint8_t BG_color=BLACK); // print c++-string at specified cursor position
  void drawText(const char * str, uint16_t pos_x, uint16_t pos_y, uint8_t FG_color=WHITE, uint8_t BG_color=BLACK); // print c-string at specified cursor position
  void drawText(char c, uint16_t pos_x, uint16_t pos_y, uint8_t FG_color=WHITE, uint8_t BG_color=BLACK); // print single character at specified cursor position
  void addText(std::string str, uint8_t FG_color=WHITE, uint8_t BG_color=BLACK); // print c++-string
  void addText(const char * str, uint8_t FG_color=WHITE, uint8_t BG_color=BLACK); // print c-string
  void addText(char c, uint8_t FG_color=WHITE, uint8_t BG_color=BLACK); // print single character
  void present(std::string s, bool clear = true);
  // high-level drawing functions
  void drawLine(uint16_t X1, uint16_t Y1, uint16_t X2, uint16_t Y2, uint8_t fgColor=WHITE);
  void drawRect(uint16_t X1, uint16_t Y1 ,uint16_t X2 ,uint16_t Y2, uint8_t fgColor=WHITE, bool filled = false);
  void drawCircle(uint16_t X1, uint16_t Y1 , uint8_t rad, uint8_t fgColor=WHITE, bool filled = false);
  void drawBMP(char const *filename); // draw a BMP bitmap picture
  
protected:
  enum DrawMode { CIRCLE_NONFILL, CIRCLE_FILL, SQUARE_NONFILL, SQUARE_FILL, LINE};  // enumeration of drawing modes
  uint8_t colorMode;
  int width, height, pixels;
  window activeWindow, textWindow, scrollWindow;
  uint8_t state;
  uint8_t cursorAutoIncrease;
  uint8_t periodicBoundary;
  uint8_t textModeSettings;
  uint8_t layerTransparancySettings; // no "set functions" yet, TODO
  uint16_t cursorX, cursorY;
  uint8_t fontLineDistance; // default: 0, can be changed with FLDR register (TODO)
  uint8_t tabSize; // size in characters of a '\t' tab; set in constructor
  uint8_t fontSizeH, fontSizeV; // default: 8, 16,  is changed by setFontSizeFactor
  
private:
  TFT tft;
  uint8_t PLL_Initial_Flag;
  uint8_t activeLayer;  // layer variable, only used in 4K mode (e.g. for dobule buffering
  
  // internal helper functions --> private
  void setRegister( uint8_t reg, uint8_t value ); //write value to a register
  uint8_t readRegister(uint8_t reg) {return reg;}; //read value from a register, TODO !!
  
  void writeCharacter(uint8_t c);
  void setHorizontal(); // used in init
  void setVertical(); // used in init
  void setColorMode(); // used in init
  void setDisplayMode(uint8_t newMode);
  void setCoordinates( uint16_t X1, uint16_t Y1 ,uint16_t X2 ,uint16_t Y2 );
  void setCoordinates( uint16_t X1, uint16_t Y1 ,uint8_t rad );
  void startDrawing( int16_t whattodraw ); // set draw mode -> see DRAW_MODES
  bool loadBMP (char const *filename, uint16_t *picture_pointer); // load BMP into memory
  
  void BTE_Foreground_Color_Red( uint8_t color );
  void BTE_Foreground_Color_Green( uint8_t color );
  void BTE_Foreground_Color_Blue( uint8_t color );
  void BTE_Background_Color_Red( uint8_t color );
  void BTE_Background_Color_Green( uint8_t color );
  void BTE_Background_Color_Blue( uint8_t color );
  // set mode for BET (Block Transfer Engine)
  void BTE_mode( uint8_t bte_operation, uint8_t rop_function );
  
  // RAIO register -> see datasheet RAIO8870
  enum Register {
    PCOD = 0x00
    , PWRR = 0x01
    , MRWC = 0x02
    , PCLK = 0x04
    , SYSR = 0x10
    , DRGB = 0x11
    , IOCR = 0x12
    , IODR = 0x13
    , HDWR  = 0x14
    , HNDFTR = 0x15
    , HNDR  = 0x16
    , HSTR  = 0x17
    , HPWR  = 0x18
    , VDHR0 = 0x19
    , VDHR1 = 0x1a
    , VNDR0 = 0x1b
    , VNDR1 = 0x1c
    , VSTR0 = 0x1d
    , VSTR1 = 0x1e
    , VPWR  = 0x1f
    , DPCR  = 0x20
    , FNCR0 = 0x21
    , FNCR1 = 0x22
    , CGSR  = 0x23
    , HOFS0 = 0x24
    , HOFS1 = 0x25
    , VOFS0 = 0x26
    , VOFS1 = 0x27
    , ROMS  = 0x28
    , FLDR  = 0x29
    , HSAW0 = 0x30
    , HSAW1 = 0x31
    , VSAW0 = 0x32
    , VSAW1 = 0x33
    , HEAW0 = 0x34
    , HEAW1 = 0x35
    , VEAW0 = 0x36
    , VEAW1 = 0x37
    , HSSW0 = 0x38
    , HSSW1 = 0x39
    , VSSW0 = 0x3a
    , VSSW1 = 0x3b
    , HESW0 = 0x3c
    , HESW1 = 0x3d
    , VESW0 = 0x3e
    , VESW1 = 0x3f
    , MWCR0 = 0x40
    , MWCR1 = 0x41
    , TFCR  = 0x42
    , TBCR  = 0x43
    , BTCR  = 0x44
    , CURS  = 0x45
    , CURH0 = 0x46
    , CURH1 = 0x47
    , CURV0 = 0x48
    , CURV1 = 0x49
    , RCURH0 = 0x4a
    , RCURH01 = 0x4b
    , RCURV0 = 0x4c
    , RCURV1 = 0x4d
    , MRCD  = 0x4e
    , BECR0 = 0x50
    , BECR1 = 0x51
    , LTPR0 = 0x52
    , LTPR1 = 0x53
    , HSBE0 = 0x54
    , HSBE1 = 0x55
    , VSBE0 = 0x56
    , VSBE1 = 0x57
    , HDBE0 = 0x58
    , HDBE1 = 0x59
    , VDBE0 = 0x5a
    , VDBE1 = 0x5b
    , BEWR0 = 0x5c
    , BEWR1 = 0x5d
    , BEHR0 = 0x5e
    , BEHR1 = 0x5f
    , BGCR0 = 0x60
    , BGCR1 = 0x61
    , BGCR2 = 0x62
    , FGCR0 = 0x63
    , FGCR1 = 0x64
    , FGCR2 = 0x65
    , PTNO  = 0x66
    , BGTR  = 0x67
    , TPCR0 = 0x70
    , TPCR1 = 0x71
    , TPXH  = 0x72
    , TPYH  = 0x73
    , TPXYL = 0x74
    , GCHP0 = 0x80
    , GCHP1 = 0x81
    , GCVP0 = 0x82
    , GCVP1 = 0x83
    , GCC0  = 0x84
    , GCC1  = 0x85
    , PLLC1 = 0x88
    , PLLC2 = 0x89
    , P1CR  = 0x8a
    , P1DCR = 0x8b
    , P2CR  = 0x8c
    , P2DCR = 0x8d
    , MCLR  = 0x8e
    , INTC  = 0x8f
    , DCR   = 0x90
    , DLHSR0 = 0x91
    , DLHSR1 = 0x92
    , DLVSR0 = 0x93
    , DLVSR1 = 0x94
    , DLHER0 = 0x95
    , DLHER1 = 0x96
    , DLVER0 = 0x97
    , DLVER1 = 0x98
    , DCHR0  = 0x99
    , DCHR1 = 0x9a
    , DCVR0 = 0x9b
    , DCVR1 = 0x9c
    , DCRR  = 0x9d
    , TCR1 = 0xa0
    , TCR2 = 0xa1
    , OEHTCR1 = 0xa2
    , OEHTCR2 = 0xa3
    , OEHTCR3 = 0xa4
    , OEHTCR4 = 0xa5
    , OEHTCR5 = 0xa6
    , OEHTCR6 = 0xa7
    , OEHTCR7 = 0xa8
    , OEHTCR8 = 0xa9
    , STHTCR1 = 0xaa
    , STHTCR2 = 0xab
    , STHTCR3 = 0xac
    , STHTCR4 = 0xad
    , Q1HCR1 = 0xae
    , Q1HCR2 = 0xaf
    , OEVTCR1 = 0xb0
    , OEVTCR2 = 0xb1
    , OEVTCR3 = 0xb2
    , OEVTCR4 = 0xb3
    , CKVTCR1 = 0xb4
    , CKVTCR2 = 0xb5
    , CKVTCR3 = 0xb6
    , CKVTCR4 = 0xb7
    , STVTCR1 = 0xb8
    , STVTCR2 = 0xb9
    , STVTCR3 = 0xba
    , STVTCR4 = 0xbb
    , STVTCR5 = 0xbc
    , STVTCR6 = 0xbd
    , STVTCR7 = 0xbe
    , STVTCR8 = 0xbf
    , COMTCR1 = 0xc0
    , COMTCR2 = 0xc1
    , RGBTCR1 = 0xc2
    , RGBTCR2 = 0xc3  };
  
};

#endif
