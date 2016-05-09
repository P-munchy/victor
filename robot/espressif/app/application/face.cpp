#include "face.h"
#include "messages.h"
#include "anki/cozmo/robot/faceDisplayDecode.h"
#include "anki/cozmo/robot/esp.h"
extern "C" {
#include "anki/cozmo/robot/drop.h"
}
#include <stdarg.h>

namespace Anki {
namespace Cozmo {
namespace Face {

  #define COLS 128
  #define ROWS 64
  #define PAGES (ROWS / 8)
  #define MAX_RECTS 4
  #define WORKING_RECTS (MAX_RECTS+1)

  typedef struct {
    u8 left;
    u8 right;
    u8 top;
    u8 bottom;
  } ScreenRect;

  typedef struct {
    bool transmitRect;
    u8 x;
    u8 y;
  } RectScanStatus;

  // 96 characters from ASCII 32 to 127, each 5x8 pixels in 5 bytes oriented vertically
  const int CHAR_WIDTH = 5, CHAR_HEIGHT = 8, CHAR_START = 32, CHAR_END = 127;
  static const u32 FONT[] ICACHE_RODATA_ATTR STORE_ATTR = {
     0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x5F,0x00,0x00, 0x00,0x07,0x00,0x07,0x00, 0x14,0x7F,0x14,0x7F,0x14,
     0x24,0x2A,0x7F,0x2A,0x12, 0x23,0x13,0x08,0x64,0x62, 0x36,0x49,0x56,0x20,0x50, 0x00,0x08,0x07,0x03,0x00,
     0x00,0x1C,0x22,0x41,0x00, 0x00,0x41,0x22,0x1C,0x00, 0x2A,0x1C,0x7F,0x1C,0x2A, 0x08,0x08,0x3E,0x08,0x08,
     0x00,0x80,0x70,0x30,0x00, 0x08,0x08,0x08,0x08,0x08, 0x00,0x00,0x60,0x60,0x00, 0x20,0x10,0x08,0x04,0x02,
     0x3E,0x51,0x49,0x45,0x3E, 0x00,0x42,0x7F,0x40,0x00, 0x72,0x49,0x49,0x49,0x46, 0x21,0x41,0x49,0x4D,0x33,
     0x18,0x14,0x12,0x7F,0x10, 0x27,0x45,0x45,0x45,0x39, 0x3C,0x4A,0x49,0x49,0x31, 0x41,0x21,0x11,0x09,0x07,
     0x36,0x49,0x49,0x49,0x36, 0x46,0x49,0x49,0x29,0x1E, 0x00,0x00,0x14,0x00,0x00, 0x00,0x40,0x34,0x00,0x00,
     0x00,0x08,0x14,0x22,0x41, 0x14,0x14,0x14,0x14,0x14, 0x00,0x41,0x22,0x14,0x08, 0x02,0x01,0x59,0x09,0x06,
     0x3E,0x41,0x5D,0x59,0x4E, 0x7C,0x12,0x11,0x12,0x7C, 0x7F,0x49,0x49,0x49,0x36, 0x3E,0x41,0x41,0x41,0x22,
     0x7F,0x41,0x41,0x41,0x3E, 0x7F,0x49,0x49,0x49,0x41, 0x7F,0x09,0x09,0x09,0x01, 0x3E,0x41,0x41,0x51,0x73,
     0x7F,0x08,0x08,0x08,0x7F, 0x00,0x41,0x7F,0x41,0x00, 0x20,0x40,0x41,0x3F,0x01, 0x7F,0x08,0x14,0x22,0x41,
     0x7F,0x40,0x40,0x40,0x40, 0x7F,0x02,0x1C,0x02,0x7F, 0x7F,0x04,0x08,0x10,0x7F, 0x3E,0x41,0x41,0x41,0x3E,
     0x7F,0x09,0x09,0x09,0x06, 0x3E,0x41,0x51,0x21,0x5E, 0x7F,0x09,0x19,0x29,0x46, 0x26,0x49,0x49,0x49,0x32,
     0x03,0x01,0x7F,0x01,0x03, 0x3F,0x40,0x40,0x40,0x3F, 0x1F,0x20,0x40,0x20,0x1F, 0x3F,0x40,0x38,0x40,0x3F,
     0x63,0x14,0x08,0x14,0x63, 0x03,0x04,0x78,0x04,0x03, 0x61,0x59,0x49,0x4D,0x43, 0x00,0x7F,0x41,0x41,0x41,
     0x02,0x04,0x08,0x10,0x20, 0x00,0x41,0x41,0x41,0x7F, 0x04,0x02,0x01,0x02,0x04, 0x40,0x40,0x40,0x40,0x40,
     0x00,0x03,0x07,0x08,0x00, 0x20,0x54,0x54,0x78,0x40, 0x7F,0x28,0x44,0x44,0x38, 0x38,0x44,0x44,0x44,0x28,
     0x38,0x44,0x44,0x28,0x7F, 0x38,0x54,0x54,0x54,0x18, 0x00,0x08,0x7E,0x09,0x02, 0x18,0xA4,0xA4,0x9C,0x78,
     0x7F,0x08,0x04,0x04,0x78, 0x00,0x44,0x7D,0x40,0x00, 0x20,0x40,0x40,0x3D,0x00, 0x7F,0x10,0x28,0x44,0x00,
     0x00,0x41,0x7F,0x40,0x00, 0x7C,0x04,0x78,0x04,0x78, 0x7C,0x08,0x04,0x04,0x78, 0x38,0x44,0x44,0x44,0x38,
     0xFC,0x18,0x24,0x24,0x18, 0x18,0x24,0x24,0x18,0xFC, 0x7C,0x08,0x04,0x04,0x08, 0x48,0x54,0x54,0x54,0x24,
     0x04,0x04,0x3F,0x44,0x24, 0x3C,0x40,0x40,0x20,0x7C, 0x1C,0x20,0x40,0x20,0x1C, 0x3C,0x40,0x30,0x40,0x3C,
     0x44,0x28,0x10,0x28,0x44, 0x4C,0x90,0x90,0x90,0x7C, 0x44,0x64,0x54,0x4C,0x44, 0x00,0x08,0x36,0x41,0x00,
     0x00,0x00,0x77,0x00,0x00, 0x00,0x41,0x36,0x08,0x00, 0x02,0x01,0x02,0x04,0x02, 0x3C,0x26,0x23,0x26,0x3C
  };

  // Height is in pages
  const int DIGIT_WIDTH = 0, DIGIT_HEIGHT = 2;
  static const u32 DIGITS[16][16] ICACHE_RODATA_ATTR STORE_ATTR = {
    { 
      0x00000000, 0x001ff000, 0x00fffe00, 0x01ffff00, 
      0x03c00780, 0x03000180, 0x03000180, 0x03000180, 
      0x03000180, 0x03c00780, 0x01ffff00, 0x00fffe00, 
      0x001ff000, 0x00000000, 0x00000000, 0x00000000
    },
    { 
      0x00000000, 0x00000000, 0x00000000, 0x03000c00, 
      0x03000c00, 0x03000e00, 0x03ffff00, 0x03ffff80, 
      0x03ffff80, 0x03000000, 0x03000000, 0x03000000, 
      0x00000000, 0x00000000, 0x00000000, 0x00000000 
    },
    { 
      0x00000000, 0x03800700, 0x03c00300, 0x03e00380, 
      0x03700180, 0x03380180, 0x031c0180, 0x030f0180, 
      0x03078380, 0x0303ff00, 0x0301ff00, 0x03007c00, 
      0x03000000, 0x00000000, 0x00000000, 0x00000000 
    },
    { 
      0x00000000, 0x01c00000, 0x01800700, 0x03000300, 
      0x03000180, 0x03018180, 0x03018180, 0x03818180, 
      0x03c3c380, 0x01ff7f00, 0x00fe7f00, 0x007c1e00, 
      0x00000000, 0x00000000, 0x00000000, 0x00000000 
    },
    { 
      0x00000000, 0x00070000, 0x00078000, 0x0006c000, 
      0x00067000, 0x00063800, 0x00060c00, 0x00060600, 
      0x00060300, 0x03ffff80, 0x03ffff80, 0x03ffff80, 
      0x00060000, 0x00060000, 0x00000000, 0x00000000 
    },
    { 
      0x00000000, 0x00000000, 0x01c00000, 0x0180ff80, 
      0x0380ff80, 0x0300ff80, 0x0300c180, 0x0300c180, 
      0x0380c180, 0x01c1c180, 0x01ff8180, 0x00ff8180, 
      0x003e0180, 0x00000000, 0x00000000, 0x00000000 
    },
    { 
      0x00000000, 0x003fe000, 0x00fff800, 0x01fffe00, 
      0x01c19f00, 0x0380c700, 0x0300c380, 0x0300c180, 
      0x0300c180, 0x03c1c180, 0x01ff8380, 0x00ff8000, 
      0x003e0000, 0x00000000, 0x00000000, 0x00000000 
    },
    { 
      0x00000000, 0x00000180, 0x02000180, 0x03800180, 
      0x03e00180, 0x01f80180, 0x007e0180, 0x001f8180, 
      0x0007e180, 0x0001f980, 0x00007f80, 0x00001f80, 
      0x00000780, 0x00000000, 0x00000000, 0x00000000 
    },
    { 
      0x00000000, 0x007c3c00, 0x00fe7e00, 0x01ffff00, 
      0x03c3e380, 0x0301c180, 0x03018180, 0x03018180, 
      0x03038180, 0x0387c380, 0x01ffff00, 0x00fe7f00, 
      0x007c3c00, 0x00000000, 0x00000000, 0x00000000 
    },
    { 
      0x00000000, 0x0000f800, 0x0003fe00, 0x0383ff00, 
      0x03070700, 0x03060180, 0x03060180, 0x03860180, 
      0x01c60380, 0x01f30700, 0x00ffff00, 0x003ffe00, 
      0x000ff800, 0x00000000, 0x00000000, 0x00000000 
    },
    { 
      0x02000000, 0x03c00000, 0x03f80000, 0x01ff0000, 
      0x003fe000, 0x001ffc00, 0x0018ff80, 0x00181f80, 
      0x00181f80, 0x0018ff80, 0x001ffc00, 0x003fe000, 
      0x01ff0000, 0x03f80000, 0x03c00000, 0x02000000 
    },
    { 
      0x00000000, 0x00000000, 0x03ffff80, 0x03ffff80, 
      0x03ffff80, 0x03018180, 0x03018180, 0x03018180, 
      0x03018180, 0x0301c380, 0x0183ff80, 0x01ff7f00, 
      0x00fe3e00, 0x007c0000, 0x00000000, 0x00000000 
    },
    { 
      0x000fe000, 0x007ff800, 0x00fffe00, 0x01f01f00, 
      0x01c00700, 0x03800380, 0x03000180, 0x03000180, 
      0x03000180, 0x03000180, 0x03000180, 0x01800300, 
      0x01c00700, 0x01e00f00, 0x00000000, 0x00000000 
    },
    { 
      0x03ffff80, 0x03ffff80, 0x03ffff80, 0x03000180, 
      0x03000180, 0x03000180, 0x03000180, 0x03000180, 
      0x03800380, 0x01800300, 0x01c00700, 0x00f01e00, 
      0x007ffc00, 0x003ff800, 0x000fe000, 0x00000000 
    },
    { 
      0x00000000, 0x03ffff80, 0x03ffff80, 0x03ffff80, 
      0x0300c180, 0x0300c180, 0x0300c180, 0x0300c180, 
      0x0300c180, 0x0300c180, 0x0300c180, 0x0300c180, 
      0x03000180, 0x00000000, 0x00000000, 0x00000000 
    },
    { 
      0x00000000, 0x03ffff80, 0x03ffff80, 0x03ffff80, 
      0x00018180, 0x00018180, 0x00018180, 0x00018180, 
      0x00018180, 0x00018180, 0x00018180, 0x00018180, 
      0x00018180, 0x00000000, 0x00000000, 0x00000000
       }
  };

  u64 m_frame[COLS];
  ScreenRect m_rects[WORKING_RECTS]; // Extra rect for working
  ScreenRect *m_activeRect;
  RectScanStatus m_scanStatus;
  int m_remainingRects;
  bool m_rectLock;

  typedef enum {
    graphics = 0x00,
    text     = 0x01,
    debug    = 0x02,
    inverted = 0x04
  } FaceMode;

  s8 _mode;
  
  static void ResetScan() {
    if (m_remainingRects > 0) {
      m_scanStatus.x = m_activeRect->left;
      m_scanStatus.y = m_activeRect->top;
      m_scanStatus.transmitRect = true;
    }
  }

  static void ResetScreen() {
    m_remainingRects = 1;
    m_activeRect = &m_rects[0];
    
    m_activeRect->left = 0;
    m_activeRect->top = 0;
    m_activeRect->right = COLS - 1;
    m_activeRect->bottom = PAGES - 1;

    memset(m_frame, 0, sizeof(m_frame));

    ResetScan();
  }

  Result Init()
  {
    // Setup a single frame transfer
    ResetScreen();

    _mode = graphics;
    return RESULT_OK;
  }
  
  #define MAX_PENALTY (-(COLS*PAGES+10))
  #define MIN(a,b) ((a < b) ? (a) : (b))
  #define MAX(a,b) ((a > b) ? (a) : (b))
  #define PIXEL(screen,x,y) (((u8*)&screen[x])[y])

  static int RectPenalty(ScreenRect& rect) {
    int width = rect.right - rect.left + 1;
    int height = rect.bottom - rect.top + 1;

    return width * height + 10;
  }

  static void MergeRects(ScreenRect& c, ScreenRect& a, ScreenRect& b) {
    c.left = MIN(a.left, b.left);
    c.top = MIN(a.top, b.top);
    c.right = MAX(a.right, b.right);
    c.bottom = MAX(a.bottom, b.bottom);
  }

  static bool ConsolidateRects(bool force = false) {
    int best_delta = MAX_PENALTY, best_a = 0, best_b = 0;
    ScreenRect best_merged;

    int a_top = m_remainingRects - 1,
        b_top = m_remainingRects;

    // Locate the best consolidation candidate
    for (int a = 0; a < a_top; a++) {
      for (int b = a+1; b < b_top; b++) {
        ScreenRect merged;
        MergeRects(merged, m_rects[a], m_rects[b]);

        // Just look at those savings
        int score =
            RectPenalty(m_rects[a])
          + RectPenalty(m_rects[b])
          - RectPenalty(merged);

        if (score > best_delta) {
          best_delta = score;
          best_merged = merged;
          best_a = a;
          best_b = b;
        }
      }
    }

    // Consolidate the rectangle down
    if (force || best_delta >= 0) {
      m_rects[best_a] = best_merged;
      m_rects[best_b] = m_rects[--m_remainingRects];
      return true;
    }

    return false;
  }

  static void CreateRects(u64* frame) {
    // We cannot create new rects while we are currently transmitting
    if (m_remainingRects > 0) return ;

    m_rectLock = true;

    m_activeRect = &m_rects[0];
    m_remainingRects = 0;

    for (int y = 0; y < PAGES; y++) {
      for (int x = 0; x < COLS; x++) {
        // Find dirty pixels
        if (PIXEL(m_frame, x, y) == PIXEL(frame, x, y)) {
          continue ;
        }

        // Create a new rectangle, expanding horizontally
        ScreenRect *working = &m_rects[m_remainingRects++];

        working->top = working->bottom = y;
        working->left = x;

        // Find bounding rectangle for horizontal strip
        // ... while coping the dirty data in
        do {
          PIXEL(m_frame, x, y) = PIXEL(frame, x, y);
          working->right = x;
        } while(++x < COLS && PIXEL(m_frame, x, y) != PIXEL(frame, x, y));

        // Keep our rectangle count down
        if (m_remainingRects > MAX_RECTS) {
          ConsolidateRects(true);
        }
      }
    }

    // Reduce total rect count
    while (ConsolidateRects()) ;

    ResetScan();

    m_rectLock = false;
  }

  // Pump face buffer data out to OLED
  extern "C" uint8_t PumpScreenData(uint8_t* dest)
  {
    static u8 transmitChain = 0;

    // Stampede PrintFormattedtection / Idle state
    if (m_rectLock || m_remainingRects == 0 || transmitChain == MAX_TX_CHAIN_COUNT) {
      transmitChain = 0;
      return 0;
    }

    // We are transmitting, so increment stampede counter
    transmitChain++;

    // We need to update the bounding box
    if (m_scanStatus.transmitRect) {
      m_scanStatus.transmitRect = false;
      memcpy(dest, (uint8_t*) m_activeRect, sizeof(ScreenRect));

      return screenDataValid | screenRectData;
    }

    // Transmit the screen bytes
    for (int i = 0; i < MAX_SCREEN_BYTES_PER_DROP; i++) {
      *(dest++) = PIXEL(m_frame, m_scanStatus.x, m_scanStatus.y);

      // Advance cursor through the rectangle
      if (++m_scanStatus.x > m_activeRect->right) {
        m_scanStatus.x = m_activeRect->left;

        if (++m_scanStatus.y > m_activeRect->bottom) {
          // Overflow to next rectangle
          m_activeRect++;
          m_remainingRects--;
          ResetScan();

          break ;
        }
      }
    }

    return screenDataValid;
  }

  void PrintFormatted(char *buffer)
  {
    // Build the result into the framebuffer
    u64 frame[COLS];
    int x = 0, y = 0;
    char* cptr = buffer;
    memset(frame, 0, sizeof(frame));

    // Go character by character until we hit the end
    while (*cptr)
    {
      // Wrap to next row, and bail out past bottom row
      int c = *cptr++;
      if (c == '\n' || x >= COLS / (CHAR_WIDTH+1))
      {
        x = 0;
        y++;
      }
      if (y >= ROWS / CHAR_HEIGHT)
        break;

      // Skip unrecognized chars
      if (c < CHAR_START || c > CHAR_END)
        continue;

      // Copy the character from the font buffer to the display buffer
      const u32* fptr = FONT + (c - CHAR_START) * CHAR_WIDTH;
      u8* gptr = (u8*)(frame) + y + x * (CHAR_WIDTH + 1) * (ROWS / CHAR_HEIGHT);

      for (int i = 0; i < CHAR_WIDTH; i++)
      {
        *gptr = *fptr++;
        gptr += ROWS / CHAR_HEIGHT;
      }
      x++;
    }
    
    if (_mode & inverted)
    {
      for (y=0; y<COLS; ++y) frame[y] = ~frame[y];
    }

    Face::CreateRects((u64*) frame);
  }


  void FaceDisplayNumber(u32 value, int x, int y)
  {
    const int TOTAL_DIGITS = 4;

    u64 frame[COLS];
    u64* pixels = &frame[x];
    memset(frame, 0, sizeof(frame));


    for (int c = 0; c < 4; c++) {
      int ch = (value >> ((TOTAL_DIGITS - 1 - c) * 4)) & 0xF;
      for (int x = 0; x < 16; x++) {
        u64 line = DIGITS[ch][x];
        *(pixels++) = line << y;
      }
    }

    Face::CreateRects((u64*) frame);
  }

  // Display text on the screen until turned off
  extern "C" void FacePrintf(const char *format, ...)
  {
    if ((_mode & debug) == 0)
      {// Build the printf into a local buffer and zero-terminate it
      char buffer[256];
      va_list argptr;
      va_start(argptr, format);
      vsnprintf(buffer, sizeof(buffer)-1, format, argptr);
      va_end(argptr);
      buffer[sizeof(buffer)-1] = '\0';
      
      _mode = text;

      PrintFormatted(buffer);
    }
  }
  
  extern "C" void FaceDebugPrintf(const char *format, ...)
  {
    // Build the printf into a local buffer and zero-terminate it
    char buffer[256];
    va_list argptr;
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer)-1, format, argptr);
    va_end(argptr);
    buffer[sizeof(buffer)-1] = '\0';
    
    _mode |= debug | inverted;

    PrintFormatted(buffer);
  }
  
  // Return display to normal function
  extern "C" void FaceUnPrintf(void)
  {
    _mode = graphics;
    m_rectLock = true;
    ResetScreen();
    m_rectLock = false;
  }

} // Face

namespace HAL {
  void FaceAnimate(u8* image, const u16 length)
  {
    if (Face::_mode != Face::graphics || Face::m_remainingRects > 0) return; // Ignore when in text mode

    else if (length == MAX_FACE_FRAME_SIZE) // If it's this size, it's raw
    {
      Face::CreateRects((u64*) image);
    }
    else
    {
      u64 frame[COLS];
      FaceDisplayDecode(image, ROWS, COLS, frame);
      Face::CreateRects(frame);
    }
  }
}

} // Cozmo
} // Anki
