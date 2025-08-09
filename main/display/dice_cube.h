#ifndef _DICE_CUBE_H_
#define _DICE_CUBE_H_

#include <lvgl.h>

struct CubeTexture {
    void *data;
    uint16_t tex_width;
    uint16_t tex_height;
};

struct CubeAxisRotation {
    float pitch;
    float yaw;
    float roll;
};

class DiceCube {
public:
    DiceCube(lv_obj_t* parent, int32_t width, int32_t height);
    ~DiceCube();
    
    bool Initialize();
    void UpdateRotation(const CubeAxisRotation& rotation);
    void UpdateDiceFace(int face_index);  // 直接更新到指定面
    void StartRollingAnimation(int final_face);  // 启动骰子旋转动画
    lv_obj_t* GetObject() { return cube_obj_; }
    
private:
    lv_obj_t* parent_;
    lv_obj_t* cube_obj_;
    int32_t width_, height_;
    CubeTexture textures_[6];
    
    bool LoadDiceTextures();
};

#endif // _DICE_CUBE_H_
