#ifndef USER_OPTIONS_H
#define USER_OPTIONS_H

/* User modifiable parameters are located in this file AND makefiles (i.e. Qt's SimpleMotionV2.pri or Makefile).
 * Be sure to check makefiles for more compile time options. The reason why options are also in makefiles is that
 * header alone can't control linking of external libraries. */

//comment out to disable, gives smaller & faster code
#define ENABLE_DEBUG_PRINTS

//max number of simultaneously opened buses. change this and recompiple SMlib if
//necessary (to increase channels or reduce to save memory)
#define SM_MAX_BUSES 10


#endif // USER_OPTIONS_H
