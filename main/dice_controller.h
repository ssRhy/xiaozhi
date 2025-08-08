#ifndef _DICE_CONTROLLER_H_
#define _DICE_CONTROLLER_H_

#include "dice_cube.h"
#include "device_state_event.h"
#include <memory>
#include <functional>
#include "esp_timer.h"

class DiceController {
public:
    static DiceController& GetInstance() {
        static DiceController instance;
        return instance;
    }
    
    DiceController(const DiceController&) = delete;
    DiceController& operator=(const DiceController&) = delete;
    
    void Initialize();
    void StartDiceMode();
    void StopDiceMode();
    bool IsActive() const { return is_active_; }
    void RollDice();  // 公共方法
    void SetAutoClose(int seconds = 10);  // 设置自动关闭时间
    void SetDiceResultCallback(std::function<void(int)> callback);  // 设置骰子结果回调
    void TriggerImuReading();  // 触发IMU读取的公共方法
    void ApplyDiceInertiaUpdate(float dice_x_set, float dice_y_set, float dice_z_set);  // 现在是公共方法
    
private:
    DiceController() = default;
    ~DiceController();  // 需要实现析构函数来清理timer
    
    std::unique_ptr<DiceCube> dice_cube_;
    bool is_active_ = false;
    
    lv_obj_t* dice_screen_ = nullptr;
    lv_obj_t* previous_screen_ = nullptr;
    esp_timer_handle_t auto_close_timer_ = nullptr;  // 自动关闭定时器
    std::function<void(int)> dice_result_callback_;  // 骰子结果回调函数
    
    void OnStateChange(DeviceState previous_state, DeviceState current_state);
    static void AutoCloseCallback(void* arg);  // 定时器回调函数
};

#endif // _DICE_CONTROLLER_H_