#include "dice_assets.h"

const char* GetDiceImageData(int face_index) {
    switch (face_index) {
        case 0: return _binary_dice1_bmp_start;
        case 1: return _binary_dice2_bmp_start;
        case 2: return _binary_dice3_bmp_start;
        case 3: return _binary_dice4_bmp_start;
        case 4: return _binary_dice5_bmp_start;
        case 5: return _binary_dice6_bmp_start;
        default: return nullptr;
    }
}

size_t GetDiceImageSize(int face_index) {
    switch (face_index) {
        case 0: return _binary_dice1_bmp_end - _binary_dice1_bmp_start;
        case 1: return _binary_dice2_bmp_end - _binary_dice2_bmp_start;
        case 2: return _binary_dice3_bmp_end - _binary_dice3_bmp_start;
        case 3: return _binary_dice4_bmp_end - _binary_dice4_bmp_start;
        case 4: return _binary_dice5_bmp_end - _binary_dice5_bmp_start;
        case 5: return _binary_dice6_bmp_end - _binary_dice6_bmp_start;
        default: return 0;
    }
}