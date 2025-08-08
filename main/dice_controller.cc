#include "dice_controller.h"
#include "esp_log.h"
#include "esp_random.h"  // 添加这个头文件来使用 esp_random()
#include "board.h"
#include "display/display.h"
#include <cmath>

static const char* TAG = "DiceController";

DiceController::~DiceController() {
    if (auto_close_timer_) {
        esp_timer_stop(auto_close_timer_);
        esp_timer_delete(auto_close_timer_);
        auto_close_timer_ = nullptr;
    }
}

void DiceController::Initialize() {
    // 初始化随机数种子 - 使用更好的随机种子
    uint32_t seed = esp_random();
    srand(seed);
    ESP_LOGI(TAG, "DiceController initialized with random seed: %u", seed);
}

void DiceController::SetDiceResultCallback(std::function<void(int)> callback) {
    dice_result_callback_ = callback;
}

void DiceController::AutoCloseCallback(void* arg) {
    DiceController* controller = static_cast<DiceController*>(arg);
    if (controller && controller->IsActive()) {
        ESP_LOGI(TAG, "Auto-closing dice mode");
        controller->StopDiceMode();
    }
}

void DiceController::SetAutoClose(int seconds) {
    // 如果已有定时器，先停止并删除
    if (auto_close_timer_) {
        esp_timer_stop(auto_close_timer_);
        esp_timer_delete(auto_close_timer_);
        auto_close_timer_ = nullptr;
    }
    
    // 创建新的定时器
    esp_timer_create_args_t timer_args = {
        .callback = &DiceController::AutoCloseCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dice_auto_close",
        .skip_unhandled_events = true
    };
    
    if (esp_timer_create(&timer_args, &auto_close_timer_) == ESP_OK) {
        ESP_LOGI(TAG, "Starting auto-close timer for %d seconds", seconds);
        esp_timer_start_once(auto_close_timer_, seconds * 1000000LL);  // 转换为微秒
    } else {
        ESP_LOGE(TAG, "Failed to create auto-close timer");
    }
}

void DiceController::StartDiceMode() {
    if (is_active_) {
        ESP_LOGW(TAG, "Dice mode already active");
        return;
    }
    
    ESP_LOGI(TAG, "Starting dice mode - taking over display");
    
    // 获取Display实例
    auto display = Board::GetInstance().GetDisplay();
    if (!display) {
        ESP_LOGE(TAG, "Failed to get display instance");
        return;
    }
    
    // 使用DisplayLockGuard保护UI操作
    {
        DisplayLockGuard lock(display);
        
        // 获取当前活动屏幕
        lv_obj_t* screen = lv_screen_active();
        if (!screen) {
            ESP_LOGE(TAG, "No active screen found");
            return;
        }
        
        // 创建覆盖整个屏幕的黑色背景
        dice_screen_ = lv_obj_create(screen);
        lv_obj_set_size(dice_screen_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(dice_screen_, 0, 0);
        lv_obj_set_style_bg_color(dice_screen_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(dice_screen_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dice_screen_, 0, 0);
        lv_obj_set_style_pad_all(dice_screen_, 0, 0);
        lv_obj_clear_flag(dice_screen_, LV_OBJ_FLAG_SCROLLABLE);
        
        // 创建骰子容器 - 居中且填充大部分屏幕
        lv_obj_t* dice_container = lv_obj_create(dice_screen_);
        lv_obj_set_size(dice_container, 240, 240);  // 使用与原项目相同的尺寸
        lv_obj_set_align(dice_container, LV_ALIGN_CENTER);
        lv_obj_set_style_bg_color(dice_container, lv_color_hex(0x000000), 0);  // 黑色背景
        lv_obj_set_style_bg_opa(dice_container, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dice_container, 0, 0);  // 无边框
        lv_obj_set_style_pad_all(dice_container, 0, 0);  // 无内边距
        lv_obj_set_style_radius(dice_container, 0, 0);  // 无圆角
        lv_obj_clear_flag(dice_container, LV_OBJ_FLAG_SCROLLABLE);
        
        // 创建骰子对象 - 使用更大的尺寸
        dice_cube_ = std::make_unique<DiceCube>(dice_container, 200, 200);
        if (!dice_cube_->Initialize()) {
            ESP_LOGE(TAG, "Failed to initialize dice cube");
            lv_obj_del(dice_screen_);
            dice_screen_ = nullptr;
            dice_cube_.reset();
            return;
        }
        
        // 居中显示骰子
        lv_obj_t* cube_obj = dice_cube_->GetObject();
        if (cube_obj) {
            lv_obj_align(cube_obj, LV_ALIGN_CENTER, 0, 0);
        }
        
        // 不添加任何文字标签，保持纯净的骰子界面
    }
    
    is_active_ = true;
    ESP_LOGI(TAG, "Dice mode started successfully");
}

void DiceController::StopDiceMode() {
    if (!is_active_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping dice mode");
    
    // 停止自动关闭定时器
    if (auto_close_timer_) {
        esp_timer_stop(auto_close_timer_);
        esp_timer_delete(auto_close_timer_);
        auto_close_timer_ = nullptr;
    }
    
    // 获取Display实例并使用锁
    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        DisplayLockGuard lock(display);
        
        // 销毁骰子相关对象
        dice_cube_.reset();
        
        // 删除覆盖层
        if (dice_screen_) {
            lv_obj_del(dice_screen_);
            dice_screen_ = nullptr;
        }
    }
    
    is_active_ = false;
    ESP_LOGI(TAG, "Dice mode stopped");
}

void DiceController::OnStateChange(DeviceState previous_state, DeviceState current_state) {
    // 根据设备状态变化处理骰子模式
    if (current_state != DeviceState::kDeviceStateIdle && is_active_) {
        // 如果设备不在空闲状态，停止骰子模式
        StopDiceMode();
    }
}

void DiceController::RollDice() {
    ESP_LOGI(TAG, "RollDice called - is_active_: %s, dice_cube_: %p", 
             is_active_ ? "true" : "false", dice_cube_.get());
    
    if (!is_active_) {
        ESP_LOGW(TAG, "RollDice failed: Dice mode is not active");
        return;
    }
    
    if (!dice_cube_) {
        ESP_LOGE(TAG, "RollDice failed: dice_cube_ is null");
        return;
    }
    
    // 使用更真正的随机数生成
    // 结合硬件随机数和当前时间来增加随机性
    uint32_t hw_random = esp_random();
    uint32_t time_random = esp_timer_get_time() & 0xFFFFFFFF;
    uint32_t combined_random = hw_random ^ time_random;
    
    int random_face = combined_random % 6;
    ESP_LOGI(TAG, "Rolling dice: hw_random=0x%x, time_random=0x%x, combined=0x%x, face=%d", 
             hw_random, time_random, combined_random, random_face + 1);
    
    dice_cube_->UpdateDiceFace(random_face);
    ESP_LOGI(TAG, "Dice rolled successfully to: %d", random_face + 1);
    
    // 通知应用程序记录骰子结果
    if (dice_result_callback_) {
        dice_result_callback_(random_face + 1);  // 传递1-6的点数
    }
    
    // 设置10秒后自动关闭
    SetAutoClose(10);
}