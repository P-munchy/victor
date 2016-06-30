#include <string.h>

#include "nrf.h"
#include "nrf_gpio.h"

#include "timer.h"
#include "lights.h"
#include "backpack.h"
#include "cubes.h"

ControllerLights lightController;

static const uint16_t DivTable[] = {
  65535, 65535, 32768, 21845, 16384, 13107, 10922,  9362,
   8192,  7281,  6553,  5957,  5461,  5041,  4681,  4369,
   4096,  3855,  3640,  3449,  3276,  3120,  2978,  2849,
   2730,  2621,  2520,  2427,  2340,  2259,  2184,  2114,  
   2048,  1985,  1927,  1872,  1820,  1771,  1724,  1680,  
   1638,  1598,  1560,  1524,  1489,  1456,  1424,  1394,  
   1365,  1337,  1310,  1285,  1260,  1236,  1213,  1191,  
   1170,  1149,  1129,  1110,  1092,  1074,  1057,  1040,  
   1024,  1008,   992,   978,   963,   949,   936,   923,  
    910,   897,   885,   873,   862,   851,   840,   829,  
    819,   809,   799,   789,   780,   771,   762,   753,  
    744,   736,   728,   720,   712,   704,   697,   689,  
    682,   675,   668,   661,   655,   648,   642,   636,  
    630,   624,   618,   612,   606,   601,   595,   590,  
    585,   579,   574,   569,   564,   560,   555,   550,  
    546,   541,   537,   532,   528,   524,   520,   516,  
    512,   508,   504,   500,   496,   492,   489,   485,  
    481,   478,   474,   471,   468,   464,   461,   458,  
    455,   451,   448,   445,   442,   439,   436,   434,  
    431,   428,   425,   422,   420,   417,   414,   412,  
    409,   407,   404,   402,   399,   397,   394,   392,  
    390,   387,   385,   383,   381,   378,   376,   374,  
    372,   370,   368,   366,   364,   362,   360,   358,  
    356,   354,   352,   350,   348,   346,   344,   343,  
    341,   339,   337,   336,   334,   332,   330,   329,  
    327,   326,   324,   322,   321,   319,   318,   316,  
    315,   313,   312,   310,   309,   307,   306,   304,  
    303,   302,   300,   299,   297,   296,   295,   293,  
    292,   291,   289,   288,   287,   286,   284,   283,  
    282,   281,   280,   278,   277,   276,   275,   274,  
    273,   271,   270,   269,   268,   267,   266,   265,  
    264,   263,   262,   261,   260,   259,   258,   257
};

static inline void AlphaBlend(
  LightSet& color, 
  const LightSet on, const LightSet off, 
  const uint16_t phase, const int frames)
{
  const int coff = phase * DivTable[frames];
  const uint8_t alpha = coff >> 8;
  const uint8_t invAlpha = ~alpha;

  color.red = (on.red * alpha + off.red * invAlpha) >> 8;
  color.green = (on.green * alpha + off.green * invAlpha) >> 8;
  color.blue = (on.blue * alpha + off.blue * invAlpha) >> 8;
  color.ir = (alpha >= 0x80) ? on.ir : off.ir;
}

static inline bool transition(const int time, int& phase, LightMode& mode, const LightMode next, const uint8_t frames) {
  phase += time;
  if (phase >= frames) {
    phase -= frames;
    mode = next;
    return true;
  }
  return false;
}

static void CalculateLEDColor(LightValues& light, const uint32_t time)
{ 
  int delta = time - light.clock;
  light.clock = time;

  if (delta <= 0) {
    return ;
  }
    
  switch (light.mode) {
    case TRANSITION_UP:
      AlphaBlend(light.values, light.onColor, light.offColor, light.phase, light.transitionOnFrames);
      
      if (transition(delta, light.phase, light.mode, HOLD_ON, light.transitionOnFrames)) {
        memcpy(&light.values, &light.onColor, sizeof(LightSet));
      }
      break ;
    case HOLD_ON:
      transition(delta, light.phase, light.mode, TRANSITION_DOWN, light.onFrames);
      break ;
    case TRANSITION_DOWN:
      AlphaBlend(light.values, light.offColor, light.onColor, light.phase, light.transitionOffFrames);
      
      if (transition(delta, light.phase, light.mode, HOLD_OFF, light.transitionOffFrames)) {
        memcpy(&light.values, &light.offColor, sizeof(LightSet));
      }
      break ;
    case HOLD_OFF:
      transition(delta, light.phase, light.mode, TRANSITION_UP, light.offFrames);
      break ;
    default:
      return ;
  }
}

void Lights::init() {  
  LightState state;
  
  // Set our default light state
  memset(&state, 0, sizeof(state));
  
  for (int i = 0; i < TOTAL_LIGHTS; i++) {
    update(lightController.lights[i], &state);
  }
}

void Lights::manage() {
  int time = GetFrame();
  
  for (int i = 0; i < TOTAL_LIGHTS; i++) {
    CalculateLEDColor(lightController.lights[i], time);
  }
}

void Lights::update(LightValues& light, const LightState* params) {
  // Convert from 5bpp to 16bpp
  LightSet onColor = { UNPACK_COLORS(params->onColor) };
  LightSet offColor = { UNPACK_COLORS(params->offColor) };
  
  memcpy(&light.onColor, &onColor, sizeof(LightSet));
  memcpy(&light.offColor, &offColor, sizeof(LightSet));
  light.onFrames = params->onFrames;
  light.offFrames = params->offFrames;
  light.transitionOnFrames = params->transitionOnFrames;
  light.transitionOffFrames = params->transitionOffFrames;
  
  // If this is a constant light
  if (params->onFrames == 255 || (params->onColor == params->offColor)) {
    light.mode = HOLD_VALUE;   
    memcpy(&light.values, &light.onColor, sizeof(LightSet));
  } else {
    light.mode = TRANSITION_UP;
    light.phase = 0;
  }
}
