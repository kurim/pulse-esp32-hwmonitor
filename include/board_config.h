#pragma once

#if defined(BOARD_CYD_2432S028R)
  #include "boards/cyd_2432s028r.h"
#elif defined(BOARD_JC8048W550)
  #include "boards/guition_jc8048w550.h"
#elif defined(BOARD_GENERIC)
  #include "boards/generic_devkit.h"
#else
  #error "Kein BOARD_* definiert - build_flags pruefen"
#endif