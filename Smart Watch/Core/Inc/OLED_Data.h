#ifndef __OLED_DATA_H
#define __OLED_DATA_H

#include "main.h"

/* Character set selection (uncomment only one) */
//#define OLED_CHARSET_UTF8
#define OLED_CHARSET_GB2312

/* Chinese glyph storage unit */
typedef struct
{
#ifdef OLED_CHARSET_UTF8
	char Index[5];         /* UTF-8 index, 5 bytes max */
#endif
#ifdef OLED_CHARSET_GB2312
	char Index[3];         /* GB2312 index, 3 bytes */
#endif
	uint8_t Data[32];      /* 16x16 glyph bitmap, vertical byte encoding */
} ChineseCell_t;

/* ASCII fonts: OLED_F6x8 and OLED_F8x16 are local (static) in oled.c */

/* Chinese font data */
extern const ChineseCell_t OLED_CF16x16[];
extern const uint8_t       OLED_CF16x16_Count;

/* Large 16x24 number font (for clock display) */
extern const uint8_t Font16x24[11][48];

/* Icon bitmaps */
extern const uint8_t IconHeart[20];
extern const uint8_t IconBattery[12];
extern const uint8_t IconBT[8];

/* Image / graphic data */
extern const uint8_t Diode[];
extern const uint8_t Return[];
extern const uint8_t Frame[];
extern const uint8_t Menu_Graph[][128];

/* Video frames (64x64, 512 bytes each, 5 frames at 10fps) */
extern const uint8_t VideoFrames64[5][512];
#define VIDEO_FRAME_COUNT  5

#endif /* __OLED_DATA_H */

