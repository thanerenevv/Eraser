#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16

#define LV_MEM_SIZE (128 * 1024U)

#define LV_DEF_REFR_PERIOD  16

#define LV_DPI_DEF 130

#define LV_DRAW_BUF_ALIGN 4

#define LV_USE_LOG 0

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_SUBPX     0
#define LV_USE_FONT_PLACEHOLDER 1

#define LV_TXT_ENC LV_TXT_ENC_UTF8

#define LV_USE_LABEL    1
#define LV_USE_IMAGE    1
#define LV_USE_ANIMIMG  0
#define LV_USE_LINE     1
#define LV_USE_TABLE    0
#define LV_USE_BTN      1
#define LV_USE_BTNMATRIX 0
#define LV_USE_BAR      1
#define LV_USE_SLIDER   1
#define LV_USE_SWITCH   1
#define LV_USE_ARC      1
#define LV_USE_DROPDOWN 1
#define LV_USE_ROLLER   0
#define LV_USE_TEXTAREA 1
#define LV_USE_CANVAS   0
#define LV_USE_CHECKBOX 1
#define LV_USE_SPAN     0
#define LV_USE_LED      0
#define LV_USE_MSGBOX   0
#define LV_USE_SPINBOX  0
#define LV_USE_SPINNER  1
#define LV_USE_TABVIEW  0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN      0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART    0
#define LV_USE_METER    0
#define LV_USE_MENU     0
#define LV_USE_LIST     1
#define LV_USE_SCALE    0

#define LV_USE_ANIM         1
#define LV_ANIM_RESOLUTION  1024

#define LV_USE_STYLE_CACHE  0

#define LV_DRAW_SW_COMPLEX  1

#define LV_USE_GPU_NXP_PXP  0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL      0

#define LV_USE_OBJ_ID       0
#define LV_USE_GROUP        1
#define LV_USE_SNAPSHOT     0
#define LV_USE_MONKEY       0
#define LV_USE_GRIDNAV      0
#define LV_USE_FRAGMENT     0

#define LV_USE_STDLIB_MALLOC    0
#define LV_USE_STDLIB_STRING    0
#define LV_USE_STDLIB_SPRINTF   0

#define LV_SPRINTF_CUSTOM 0

#define LV_COLOR_SCREEN_TRANSP 0

#define LV_OBJ_COORDS_LIMIT 4096

#define LV_LAYER_SIMPLE_BUF_SIZE (24 * 1024)

#define LV_IMG_CACHE_DEF_SIZE 0

#define LV_GRADIENT_MAX_STOPS 2

#define LV_USE_DRAW_SW 1

#endif

#endif
