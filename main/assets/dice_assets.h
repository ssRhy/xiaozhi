#ifndef _DICE_ASSETS_H_
#define _DICE_ASSETS_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 骰子图片资源的外部声明
// 这些符号会由ESP-IDF的构建系统自动生成
extern const char _binary_dice1_bmp_start[] asm("_binary_dice1_bmp_start");
extern const char _binary_dice1_bmp_end[] asm("_binary_dice1_bmp_end");

extern const char _binary_dice2_bmp_start[] asm("_binary_dice2_bmp_start");
extern const char _binary_dice2_bmp_end[] asm("_binary_dice2_bmp_end");

extern const char _binary_dice3_bmp_start[] asm("_binary_dice3_bmp_start");
extern const char _binary_dice3_bmp_end[] asm("_binary_dice3_bmp_end");

extern const char _binary_dice4_bmp_start[] asm("_binary_dice4_bmp_start");
extern const char _binary_dice4_bmp_end[] asm("_binary_dice4_bmp_end");

extern const char _binary_dice5_bmp_start[] asm("_binary_dice5_bmp_start");
extern const char _binary_dice5_bmp_end[] asm("_binary_dice5_bmp_end");

extern const char _binary_dice6_bmp_start[] asm("_binary_dice6_bmp_start");
extern const char _binary_dice6_bmp_end[] asm("_binary_dice6_bmp_end");

// 帮助函数：获取骰子图片数据
const char* GetDiceImageData(int face_index);
size_t GetDiceImageSize(int face_index);

#ifdef __cplusplus
}
#endif

#endif // _DICE_ASSETS_H_