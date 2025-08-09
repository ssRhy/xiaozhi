#include "dice_controller.h"
#include "esp_log.h"
#include "esp_random.h"  // 添加这个头文件来使用 esp_random()
#include "board.h"
#include "display/display.h"
#include <cmath>

extern "C" {
#include "boards/common/app_imu.h"
}

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
    // 不再使用ImuSensor类，改用真正的BMI270驱动
}

void DiceController::SetDiceResultCallback(std::function<void(int)> callback) {
    dice_result_callback_ = callback;
}

void DiceController::AutoCloseCallback(void* arg) {
    DiceController* controller = static_cast<DiceController*>(arg);
    if (controller && controller->IsActive()) {
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
        esp_timer_start_once(auto_close_timer_, seconds * 1000000LL);  // 转换为微秒
    }
}

void DiceController::StartDiceMode() {
    if (is_active_) {
        return;
    }
    
    // 尝试安全初始化IMU
    app_imu_init();
    
    // 获取Display实例
    auto display = Board::GetInstance().GetDisplay();
    if (!display) {
        return;
    }
    
    // 使用DisplayLockGuard保护UI操作
    {
        DisplayLockGuard lock(display);
        
        // 获取当前活动屏幕
        lv_obj_t* screen = lv_screen_active();
        if (!screen) {
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
}

void DiceController::StopDiceMode() {
    if (!is_active_) {
        return;
    }
    
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
    
    // ESP SparkBot模式：IMU任务持续运行，不需要清理
}

void DiceController::OnStateChange(DeviceState previous_state, DeviceState current_state) {
    // 根据设备状态变化处理骰子模式
    if (current_state != DeviceState::kDeviceStateIdle && is_active_) {
        // 如果设备不在空闲状态，停止骰子模式
        StopDiceMode();
    }
}

void DiceController::RollDice() {
    if (!is_active_) {
        return;
    }
    
    if (!dice_cube_) {
        return;
    }
    
    // 使用更真正的随机数生成
    // 结合硬件随机数和当前时间来增加随机性
    uint32_t hw_random = esp_random();
    uint32_t time_random = esp_timer_get_time() & 0xFFFFFFFF;
    uint32_t combined_random = hw_random ^ time_random;
    
    int random_face = combined_random % 6;
    
    // 启动骰子旋转动画而不是直接更新面
    dice_cube_->StartRollingAnimation(random_face);
    
    // 通知应用程序记录骰子结果
    if (dice_result_callback_) {
        dice_result_callback_(random_face + 1);  // 传递1-6的点数
    }
    
    // 设置15秒后自动关闭（给动画更多时间）
    SetAutoClose(15);
}

// 使用ESP SparkBot的骰子惯性算法
void DiceController::ApplyDiceInertiaUpdate(float dice_x_set, float dice_y_set, float dice_z_set) {
    if (!is_active_ || !dice_cube_) {
        return;
    }
    
    // 直接调用移植的C函数
    bmi270_axis_t dice_axis = applyDiceInertia(dice_x_set, dice_y_set, dice_z_set);
    
    // 将骰子旋转数据发送给骰子立方体进行实时显示
    CubeAxisRotation rotation;
    rotation.pitch = dice_axis.pitch;
    rotation.yaw = dice_axis.yaw;
    rotation.roll = dice_axis.roll;
    
    dice_cube_->UpdateRotation(rotation);
}

void DiceController::TriggerImuReading() {
    // 这个方法现在不再需要，因为我们使用了真正的BMI270驱动
}

// 新的C接口函数，直接处理来自BMI270的IMU数据
extern "C" void trigger_dice_with_imu_data(float pitch, float roll, float yaw) {
    auto& dice_controller = DiceController::GetInstance();
    
    // 双重检查：确保骰子模式确实激活
    if (!dice_controller.IsActive()) {
        return;  // 骰子模式未激活，完全忽略IMU数据
    }
    
    // 使用ESP SparkBot的算法处理IMU数据 - 仿照app_dice_event
    dice_controller.ApplyDiceInertiaUpdate(pitch * 15, roll * 15, yaw * 15);
    
    // 检测强烈摇晃以触发骰子投掷
    float shake_magnitude = fabsf(pitch) + fabsf(roll) + fabsf(yaw);
    static float last_shake_time = 0;
    float current_time = esp_timer_get_time() / 1000000.0f; // 转换为秒
    
    // 使用更高的摇晃阈值，避免误触发
    const float SHAKE_THRESHOLD = 20.0f;  // 提高阈值到20度/秒
    const float DEBOUNCE_TIME = 2.0f;     // 增加去抖时间到2秒
    
    if (shake_magnitude > SHAKE_THRESHOLD && (current_time - last_shake_time) > DEBOUNCE_TIME) {
        dice_controller.RollDice();
        last_shake_time = current_time;
    }
}

// 保留旧的接口以保持兼容性
extern "C" void trigger_dice_imu_update(void) {
    // 这个函数现在不再使用，但保留以避免链接错误
}

// C接口函数，用于检查骰子模式是否激活
extern "C" bool is_dice_mode_active(void) {
    return DiceController::GetInstance().IsActive();
}