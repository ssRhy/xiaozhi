#include "dice_cube.h"
#include "assets/dice_assets.h"
#include "esp_log.h"
#include <cstdlib>
#include <cstring>
#include <cmath>

static const char* TAG = "DiceCube";

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
    ESP_LOGI(TAG, "Initializing DiceCube");
    
    // 创建图片对象 - LVGL 9.x 使用 lv_image_create
    cube_obj_ = lv_image_create(parent_);
    if (!cube_obj_) {
        ESP_LOGE(TAG, "Failed to create cube object");
        return false;
    }
    
    // 设置对象大小
    lv_obj_set_size(cube_obj_, width_, height_);
    
    // 加载骰子纹理
    if (!LoadDiceTextures()) {
        ESP_LOGE(TAG, "Failed to load dice textures");
        return false;
    }
    
    // 显示第一个面（骰子1）
    UpdateDiceFace(0);
    
    ESP_LOGI(TAG, "DiceCube initialized successfully");
    return true;
}

void DiceCube::UpdateRotation(const CubeAxisRotation& rotation) {
    // 根据旋转角度确定显示哪个面
    int face_index = 0;
    
    // 简单的面选择逻辑
    float abs_pitch = fabs(rotation.pitch);
    float abs_yaw = fabs(rotation.yaw);
    float abs_roll = fabs(rotation.roll);
    
    if (abs_pitch > abs_yaw && abs_pitch > abs_roll) {
        face_index = rotation.pitch > 0 ? 1 : 2;
    } else if (abs_yaw > abs_roll) {
        face_index = rotation.yaw > 0 ? 3 : 4;
    } else {
        face_index = rotation.roll > 0 ? 5 : 0;
    }
    
    // 确保索引在有效范围内
    face_index = face_index % 6;
    
    // 更新显示的图片
    UpdateDiceFace(face_index);
}

void DiceCube::UpdateDiceFace(int face_index) {
    if (face_index < 0 || face_index >= 6 || !cube_obj_) {
        ESP_LOGE(TAG, "Invalid face_index %d or null cube_obj", face_index);
        return;
    }
    
    // 获取骰子图片数据
    const char* image_data = GetDiceImageData(face_index);
    size_t image_size = GetDiceImageSize(face_index);
    
    if (!image_data || image_size == 0) {
        ESP_LOGE(TAG, "Failed to get dice image data for face %d", face_index + 1);
        return;
    }
    
    ESP_LOGI(TAG, "Setting dice face %d, image_data=%p, size=%zu", 
             face_index + 1, image_data, image_size);
    
    // 跳过BMP文件头（54字节），就像原始项目中一样
    const char* bmp_pixel_data = image_data + 54;
    size_t pixel_data_size = image_size - 54;
    
    ESP_LOGI(TAG, "BMP pixel data: %p, size: %zu", bmp_pixel_data, pixel_data_size);
    
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
    
    ESP_LOGI(TAG, "Successfully updated dice face to %d", face_index + 1);
}

bool DiceCube::LoadDiceTextures() {
    ESP_LOGI(TAG, "Loading dice textures...");
    
    // 验证所有资源是否可用
    for (int i = 0; i < 6; i++) {
        const char* image_data = GetDiceImageData(i);
        size_t image_size = GetDiceImageSize(i);
        
        if (!image_data || image_size == 0) {
            ESP_LOGE(TAG, "Failed to load dice image %d", i + 1);
            return false;
        }
        
        // 检查BMP文件头
        if (image_size < 54) {
            ESP_LOGE(TAG, "Image %d too small (%zu bytes), not a valid BMP", i + 1, image_size);
            return false;
        }
        
        // 检查BMP签名
        if (image_data[0] != 'B' || image_data[1] != 'M') {
            ESP_LOGW(TAG, "Image %d may not be a valid BMP file", i + 1);
        }
        
        ESP_LOGI(TAG, "Verified dice texture %d: data=%p, size=%zu", 
                 i + 1, image_data, image_size);
    }
    
    ESP_LOGI(TAG, "All dice textures verified successfully");
    return true;
}