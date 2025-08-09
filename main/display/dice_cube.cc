#include "dice_cube.h"
#include "assets/dice_assets.h"
#include "esp_log.h"
#include <cstdlib>
#include <cstring>
#include <cmath>

static const char* TAG = "DiceCube";

// 骰子旋转动画参数
static bool is_rolling = false;
static float current_x_rotation = 0.0f;
static float current_y_rotation = 0.0f;
static float current_z_rotation = 0.0f;
static int32_t target_face = 0;
static int32_t animation_frames_left = 0;

// 动画定时器回调函数
static void dice_animation_timer_cb(lv_timer_t* timer) {
    DiceCube* cube = (DiceCube*)lv_timer_get_user_data(timer);
    if (!cube || !is_rolling || animation_frames_left <= 0) {
        is_rolling = false;
        lv_timer_del(timer);
        return;
    }
    
    // 更新旋转角度
    current_x_rotation += 5.0f + (animation_frames_left * 0.5f);
    current_y_rotation += 7.0f + (animation_frames_left * 0.3f);
    current_z_rotation += 3.0f + (animation_frames_left * 0.7f);
    
    // 模拟旋转减速
    animation_frames_left--;
    
    // 当动画结束时，显示目标面
    if (animation_frames_left <= 0) {
        is_rolling = false;
        cube->UpdateDiceFace(target_face);
        lv_timer_del(timer);
    } else {
        // 继续显示旋转中的随机面
        int random_face = (int)(current_x_rotation + current_y_rotation) % 6;
        cube->UpdateDiceFace(random_face);
    }
}

// DiceCube类的实现
DiceCube::DiceCube(lv_obj_t* parent, int32_t width, int32_t height)
    : parent_(parent), cube_obj_(nullptr), width_(width), height_(height) {
    memset(textures_, 0, sizeof(textures_));
}

DiceCube::~DiceCube() {
    if (cube_obj_) {
        lv_obj_del(cube_obj_);
    }
}

bool DiceCube::Initialize() {
    // 创建图片对象 - LVGL 9.x 使用 lv_image_create
    cube_obj_ = lv_image_create(parent_);
    if (!cube_obj_) {
        return false;
    }
    
    // 设置对象大小
    lv_obj_set_size(cube_obj_, width_, height_);
    
    // 加载骰子纹理
    if (!LoadDiceTextures()) {
        return false;
    }
    
    // 显示第一个面（骰子1）
    UpdateDiceFace(0);
    
    return true;
}

void DiceCube::UpdateRotation(const CubeAxisRotation& rotation) {
    // 如果正在进行投掷动画，不处理旋转更新
    if (is_rolling) {
        return;
    }
    
    // 根据旋转角度确定显示哪个面
    // 使用ESP SparkBot类似的面选择算法
    int face_index = 0;
    
    // 将旋转角度映射到骰子面
    // 每个90度对应一个面的变化
    float pitch_face = fmodf(fabsf(rotation.pitch), 360.0f) / 90.0f;
    float yaw_face = fmodf(fabsf(rotation.yaw), 360.0f) / 90.0f;
    float roll_face = fmodf(fabsf(rotation.roll), 360.0f) / 90.0f;
    
    // 根据最主要的旋转轴确定面
    float abs_pitch = fabsf(rotation.pitch);
    float abs_yaw = fabsf(rotation.yaw);
    float abs_roll = fabsf(rotation.roll);
    
    if (abs_pitch > abs_yaw && abs_pitch > abs_roll) {
        // 主要是pitch旋转
        face_index = ((int)pitch_face) % 6;
    } else if (abs_yaw > abs_roll) {
        // 主要是yaw旋转
        face_index = ((int)yaw_face + 2) % 6;
    } else {
        // 主要是roll旋转
        face_index = ((int)roll_face + 4) % 6;
    }
    
    // 确保索引在有效范围内
    face_index = face_index % 6;
    
    // 更新显示的图片
    UpdateDiceFace(face_index);
}

void DiceCube::UpdateDiceFace(int face_index) {
    if (face_index < 0 || face_index >= 6 || !cube_obj_) {
        return;
    }
    
    // 获取骰子图片数据
    const char* image_data = GetDiceImageData(face_index);
    size_t image_size = GetDiceImageSize(face_index);
    
    if (!image_data || image_size == 0) {
        return;
    }
    
    // 跳过BMP文件头（54字节），就像原始项目中一样
    const char* bmp_pixel_data = image_data + 54;
    size_t pixel_data_size = image_size - 54;
    
    // 创建图片描述符，使用正确的BMP格式参数
    static lv_image_dsc_t img_dsc;
    img_dsc.header.cf = LV_COLOR_FORMAT_RGB888;  // BMP是24位RGB888格式
    img_dsc.header.w = 120;   // BMP图片实际宽度是120像素
    img_dsc.header.h = 120;   // BMP图片实际高度是120像素
    img_dsc.data_size = pixel_data_size;
    img_dsc.data = (const uint8_t*)bmp_pixel_data;
    
    // 设置图片源
    lv_image_set_src(cube_obj_, &img_dsc);
    
    // 居中显示并缩放到容器大小
    lv_obj_set_size(cube_obj_, width_, height_);
    lv_obj_center(cube_obj_);
}

// 启动骰子旋转动画
void DiceCube::StartRollingAnimation(int final_face) {
    if (is_rolling) {
        return;  // 如果已经在旋转中，不重复启动
    }
    
    is_rolling = true;
    target_face = final_face % 6;
    animation_frames_left = 60;  // 约3秒的动画（60帧 * 50ms）
    
    // 重置旋转角度
    current_x_rotation = 0.0f;
    current_y_rotation = 0.0f;
    current_z_rotation = 0.0f;
    
    // 启动动画定时器
    lv_timer_create(dice_animation_timer_cb, 50, this);
    
    ESP_LOGI(TAG, "Started dice rolling animation to face %d", final_face + 1);
}

bool DiceCube::LoadDiceTextures() {
   
    
    // 验证所有资源是否可用
    for (int i = 0; i < 6; i++) {
        const char* image_data = GetDiceImageData(i);
        size_t image_size = GetDiceImageSize(i);
        
        if (!image_data || image_size == 0) {
           
            return false;
        }
        
        // 检查BMP文件头
        if (image_size < 54) {
            
            return false;
        }
        
        // 检查BMP签名
        if (image_data[0] != 'B' || image_data[1] != 'M') {
            // 可能不是有效的BMP文件，但继续尝试
        }
        
        
    }
    
    
    return true;
}