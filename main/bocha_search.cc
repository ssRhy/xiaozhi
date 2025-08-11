/*
 * Bocha AI Search Tool Implementation
 * 提供基于 MCP 协议的 Bocha AI 搜索功能
 */

#include "bocha_search.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "BOCHA_SEARCH"
#define BOCHA_API_URL "https://api.bochaai.com/v1/web-search"
#define BOCHA_API_KEY "sk-cfb631d87cb44b3a84e9b6eae3d4a8c8"
#define MAX_RESPONSE_SIZE 32768  // 增加到32KB
#define HTTP_TIMEOUT_MS 30000  // 30 秒超时
#define MAX_RETRIES 3          // 最大重试次数
#define RETRY_DELAY_MS 1000    // 重试间隔

// HTTP 事件处理器数据结构
struct http_event_data {
    std::string* response_buffer;
    int total_len;
    bool error_occurred;
    bool was_truncated;
};

void BochaSearch::RegisterTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    mcp_server.AddTool(
        "self.search.bocha",
        "使用 Bocha AI 进行网络搜索，获取准确和最新的信息。\n"
        "参数说明:\n"
        "- query: 搜索关键词(必填)\n"
        "- count: 返回结果数量限制(1-4, 默认2)\n"
        "使用场景:\n"
        "1. 搜索网络信息和资料\n"
        "2. 获取实时资讯和技术文档\n"
        "3. 查找特定主题的详细信息\n"
        "4. 获取最新新闻和时事",
        PropertyList({
            Property("query", kPropertyTypeString),  // 必填参数
            Property("count", kPropertyTypeInteger, 2, 1, 4)  // 可选参数，默认值2，范围1-4
        }),
        DoSearch
    );

    // 添加专门的服装搜索工具
    mcp_server.AddTool(
        "self.search.outfit",
        "专门用于服装穿搭的智能搜索，根据风格、颜色、季节等维度推荐衣物。\n"
        "参数说明:\n"
        "- style: 服装风格(如休闲、正式、运动、时尚等)\n"
        "- colors: 颜色搭配(如黑色、白色、蓝色等)\n"
        "- season: 适合季节(春夏秋冬)\n"
        "- item_type: 服装类型(如上衣、裤子、鞋子、配饰等)\n"
        "- count: 返回结果数量(1-4, 默认3)\n"
        "使用场景:\n"
        "1. 根据现有穿搭寻找配套衣物\n"
        "2. 按特定风格搜索服装\n"
        "3. 季节性服装推荐\n"
        "4. 特定场合的穿搭建议",
        PropertyList({
            Property("style", kPropertyTypeString, ""),
            Property("colors", kPropertyTypeString, ""),
            Property("season", kPropertyTypeString, ""),
            Property("item_type", kPropertyTypeString, ""),
            Property("count", kPropertyTypeInteger, 3, 1, 4)
        }),
        DoOutfitSearch
    );

    ESP_LOGI(TAG, "Bocha AI search tool registered successfully");
}

// HTTP 事件处理器
esp_err_t BochaSearch::HttpEventHandler(esp_http_client_event_t *evt) {
    http_event_data* event_data = (http_event_data*)evt->user_data;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            event_data->error_occurred = true;
            break;
            
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
            
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
            
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", 
                    evt->header_key, evt->header_value);
            break;
            
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (event_data->response_buffer && evt->data_len > 0) {
                if (event_data->total_len + evt->data_len <= MAX_RESPONSE_SIZE) {
                    event_data->response_buffer->append((char*)evt->data, evt->data_len);
                    event_data->total_len += evt->data_len;
                    ESP_LOGD(TAG, "Appended %d bytes, total: %d", evt->data_len, event_data->total_len);
                } else {
                    // 计算可以安全添加的字节数，预留空间给JSON结束符
                    int safe_remaining = MAX_RESPONSE_SIZE - event_data->total_len - 100; // 预留100字节
                    if (safe_remaining > 0) {
                        event_data->response_buffer->append((char*)evt->data, std::min(safe_remaining, evt->data_len));
                        event_data->total_len += std::min(safe_remaining, evt->data_len);
                        ESP_LOGW(TAG, "Response truncated to %d bytes to preserve JSON structure", event_data->total_len);
                    }
                    // 标记为截断，但不设置错误
                    event_data->was_truncated = true;
                    ESP_LOGW(TAG, "Response too large, safely truncated at %d bytes", event_data->total_len);
                }
            }
            break;
            
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH, total received: %d bytes", event_data->total_len);
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

std::string BochaSearch::UrlEncode(const std::string& str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << std::uppercase;
            encoded << '%' << std::setw(2) << int((unsigned char)c);
            encoded << std::nouppercase;
        }
    }

    return encoded.str();
}

std::string BochaSearch::BuildSearchPayload(const std::string& query, int count, bool summary) {
    cJSON* payload = cJSON_CreateObject();
    
    // 检查是否是服装搜索，如果是则优化查询
    std::string optimized_query = query;
    if (query.find("服装") != std::string::npos || 
        query.find("穿搭") != std::string::npos || 
        query.find("搭配") != std::string::npos ||
        query.find("时尚") != std::string::npos) {
        // 为服装搜索添加购物相关关键词
        optimized_query += " 购买 商品 价格 品牌 店铺";
    }
    
    cJSON_AddStringToObject(payload, "query", optimized_query.c_str());
    cJSON_AddStringToObject(payload, "freshness", "noLimit");
    cJSON_AddBoolToObject(payload, "summary", summary);
    cJSON_AddNumberToObject(payload, "count", count);
    
    char* json_str = cJSON_PrintUnformatted(payload);
    std::string result(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(payload);
    
    return result;
}

std::string BochaSearch::HttpPost(const std::string& payload) {
    ESP_LOGI(TAG, "Making HTTP POST request to Bocha AI");
    ESP_LOGI(TAG, "Request payload: %s", payload.c_str());
    
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        if (retry > 0) {
            ESP_LOGI(TAG, "Retry attempt %d/%d after %d ms", retry + 1, MAX_RETRIES, RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        }
        
        // 准备事件数据
        std::string response_buffer;
        http_event_data event_data = {
            .response_buffer = &response_buffer,
            .total_len = 0,
            .error_occurred = false,
            .was_truncated = false
        };
        
        esp_http_client_config_t config = {};
        config.url = BOCHA_API_URL;
        config.method = HTTP_METHOD_POST;
        config.timeout_ms = HTTP_TIMEOUT_MS;
        config.buffer_size = MAX_RESPONSE_SIZE;
        config.user_agent = "XiaoZhi-ESP32/1.0";
        config.transport_type = HTTP_TRANSPORT_OVER_SSL;
        config.skip_cert_common_name_check = true;
        config.event_handler = HttpEventHandler;
        config.user_data = &event_data;
        
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "Failed to initialize HTTP client");
            continue;
        }
        
        // 设置 HTTP 头
        esp_http_client_set_header(client, "Authorization", "Bearer " BOCHA_API_KEY);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "Accept", "application/json");
        
        // 设置 POST 数据
        esp_http_client_set_post_field(client, payload.c_str(), payload.length());
        
        esp_err_t err = esp_http_client_perform(client);
        
        if (err == ESP_OK && !event_data.error_occurred) {
            int status_code = esp_http_client_get_status_code(client);
            int content_length = esp_http_client_get_content_length(client);
            
            ESP_LOGI(TAG, "HTTP Status: %d, Content Length: %d, Received: %d bytes", 
                    status_code, content_length, event_data.total_len);
            
            if (status_code == 200 && event_data.total_len > 0) {
                ESP_LOGI(TAG, "Successfully received response from Bocha AI (%d bytes)%s", 
                        event_data.total_len, event_data.was_truncated ? " [TRUNCATED]" : "");
                ESP_LOGD(TAG, "Response preview: %.200s%s", response_buffer.c_str(), 
                        response_buffer.length() > 200 ? "..." : "");
                esp_http_client_cleanup(client);
                return response_buffer;
            } else if (status_code == 200) {
                ESP_LOGE(TAG, "HTTP 200 but no data received");
            } else {
                ESP_LOGW(TAG, "HTTP request failed with status: %d", status_code);
            }
        } else {
            ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
            if (event_data.error_occurred) {
                ESP_LOGE(TAG, "HTTP event handler reported an error");
            }
        }
        
        esp_http_client_cleanup(client);
    }
    
    ESP_LOGE(TAG, "All retry attempts failed");
    return "";
}

std::string BochaSearch::ParseSearchResults(const std::string& json_content) {
    ESP_LOGD(TAG, "Parsing JSON response (%zu bytes)", json_content.length());
    
    // 检查JSON是否被截断，尝试修复
    std::string fixed_json = json_content;
    
    // 如果JSON被截断，尝试找到最后一个完整的结果数组项并正确关闭JSON
    if (json_content.length() >= MAX_RESPONSE_SIZE - 200) {  // 可能被截断
        ESP_LOGW(TAG, "JSON might be truncated, attempting to fix");
        
        // 找到最后一个完整的结果项
        size_t last_complete_item = fixed_json.rfind("},{");
        if (last_complete_item != std::string::npos) {
            // 截断到最后一个完整项，然后添加适当的JSON结构结束符
            fixed_json = fixed_json.substr(0, last_complete_item + 1);
            fixed_json += "]}}}";  // 关闭 value数组, webPages对象, data对象, 根对象
            ESP_LOGI(TAG, "Fixed truncated JSON, new length: %zu bytes", fixed_json.length());
        }
    }
    
    // 解析 Bocha AI API 返回的 JSON
    cJSON* bocha_json = cJSON_Parse(fixed_json.c_str());
    if (!bocha_json) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        ESP_LOGD(TAG, "JSON preview: %.500s%s", fixed_json.c_str(), 
                fixed_json.length() > 500 ? "..." : "");
        return "{\"status\": \"error\", \"message\": \"解析搜索结果失败\"}";
    }

    // 创建我们的结果对象
    cJSON* result = cJSON_CreateObject();
    
    // 添加搜索状态信息
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddStringToObject(result, "search_engine", "Bocha AI");
    
    // 检查响应代码
    auto code = cJSON_GetObjectItem(bocha_json, "code");
    if (!code || !cJSON_IsNumber(code) || code->valueint != 200) {
        ESP_LOGE(TAG, "API returned error code: %d", code ? code->valueint : -1);
        auto msg = cJSON_GetObjectItem(bocha_json, "msg");
        std::string error_msg = "API 错误";
        if (msg && cJSON_IsString(msg)) {
            error_msg = msg->valuestring;
        }
        cJSON_Delete(bocha_json);
        return "{\"status\": \"error\", \"message\": \"" + error_msg + "\"}";
    }
    
    // 获取搜索结果
    auto data = cJSON_GetObjectItem(bocha_json, "data");
    if (!data || !cJSON_IsObject(data)) {
        ESP_LOGE(TAG, "No data object found in response");
        cJSON_Delete(bocha_json);
        return "{\"status\": \"error\", \"message\": \"响应格式错误\"}";
    }
    
    auto webPages = cJSON_GetObjectItem(data, "webPages");
    if (!webPages || !cJSON_IsObject(webPages)) {
        ESP_LOGE(TAG, "No webPages object found in response");
        cJSON_Delete(bocha_json);
        return "{\"status\": \"error\", \"message\": \"未找到搜索结果\"}";
    }
    
    auto results = cJSON_GetObjectItem(webPages, "value");
    if (!results || !cJSON_IsArray(results)) {
        ESP_LOGE(TAG, "No value array found in webPages");
        cJSON_Delete(bocha_json);
        return "{\"status\": \"error\", \"message\": \"未找到搜索结果\"}";
    }
    
    // 添加结果数量
    int count = cJSON_GetArraySize(results);
    cJSON_AddNumberToObject(result, "total_results", count);
    
    // 处理搜索结果
    cJSON* processed_results = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(results, i);
        if (!cJSON_IsObject(item)) continue;
        
        cJSON* processed_item = cJSON_CreateObject();
        
        // 提取标题 (使用 name 字段)
        auto name = cJSON_GetObjectItem(item, "name");
        if (cJSON_IsString(name)) {
            cJSON_AddStringToObject(processed_item, "title", name->valuestring);
        }
        
        // 提取链接
        auto url = cJSON_GetObjectItem(item, "url");
        if (cJSON_IsString(url)) {
            cJSON_AddStringToObject(processed_item, "link", url->valuestring);
        }
        
        // 提取摘要
        auto snippet = cJSON_GetObjectItem(item, "snippet");
        if (cJSON_IsString(snippet)) {
            cJSON_AddStringToObject(processed_item, "snippet", snippet->valuestring);
        }
        
        // 提取站点名称
        auto siteName = cJSON_GetObjectItem(item, "siteName");
        if (cJSON_IsString(siteName)) {
            cJSON_AddStringToObject(processed_item, "siteName", siteName->valuestring);
        }
        
        cJSON_AddItemToArray(processed_results, processed_item);
    }
    
    cJSON_AddItemToObject(result, "results", processed_results);
    
    // 添加查询信息
    auto queryContext = cJSON_GetObjectItem(data, "queryContext");
    if (queryContext && cJSON_IsObject(queryContext)) {
        auto originalQuery = cJSON_GetObjectItem(queryContext, "originalQuery");
        if (cJSON_IsString(originalQuery)) {
            cJSON_AddStringToObject(result, "query", originalQuery->valuestring);
        }
    }
    
    // 添加总匹配数量
    auto totalMatches = cJSON_GetObjectItem(webPages, "totalEstimatedMatches");
    if (totalMatches && cJSON_IsNumber(totalMatches)) {
        cJSON_AddNumberToObject(result, "totalEstimatedMatches", totalMatches->valueint);
    }
    
    // 转换为字符串
    char* json_str = cJSON_PrintUnformatted(result);
    std::string result_str(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(result);
    cJSON_Delete(bocha_json);
    
    return result_str;
}

ReturnValue BochaSearch::DoSearch(const PropertyList& properties) {
    try {
        std::string query = properties["query"].value<std::string>();
        int count = properties["count"].value<int>();
        
        ESP_LOGI(TAG, "Performing Bocha AI search: query='%s', count=%d", 
                query.c_str(), count);
        
        // 验证输入
        if (query.empty()) {
            ESP_LOGE(TAG, "Search query is empty");
            return "{\"status\": \"error\", \"message\": \"搜索关键词不能为空\"}";
        }
        
        // 构建搜索请求体
        std::string search_payload = BuildSearchPayload(query, count, true);
        
        // 执行 HTTP POST 请求
        std::string json_content = HttpPost(search_payload);
        
        if (json_content.empty()) {
            ESP_LOGE(TAG, "Failed to get search results");
            return "{\"status\": \"error\", \"message\": \"无法获取搜索结果，请检查网络连接\"}";
        }
        
        // 解析搜索结果
        std::string results = ParseSearchResults(json_content);
        
        ESP_LOGI(TAG, "Bocha AI search completed successfully");
        return results;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Search failed: %s", e.what());
        std::string error_msg = "{\"status\": \"error\", \"message\": \"搜索失败: ";
        error_msg += e.what();
        error_msg += "\"}";
        return error_msg;
    }
}

std::string BochaSearch::BuildOutfitSearchPayload(const std::string& style, const std::string& colors, 
                                                  const std::string& season, const std::string& item_type, int count) {
    cJSON* payload = cJSON_CreateObject();
    
    // 构建专门的服装搜索查询
    std::string query;
    
    // 添加风格
    if (!style.empty()) {
        query += style + "风格 ";
    }
    
    // 添加颜色
    if (!colors.empty()) {
        query += colors + " ";
    }
    
    // 添加季节
    if (!season.empty()) {
        query += season + " ";
    }
    
    // 添加服装类型
    if (!item_type.empty()) {
        query += item_type + " ";
    }
    
    // 添加购物和品牌相关关键词
    query += "服装 穿搭 搭配 购买 商品 价格 品牌 店铺 时尚 推荐";
    
    cJSON_AddStringToObject(payload, "query", query.c_str());
    cJSON_AddStringToObject(payload, "freshness", "noLimit");
    cJSON_AddBoolToObject(payload, "summary", true);
    cJSON_AddNumberToObject(payload, "count", count);
    
    char* json_str = cJSON_PrintUnformatted(payload);
    std::string result(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(payload);
    
    return result;
}

ReturnValue BochaSearch::DoOutfitSearch(const PropertyList& properties) {
    try {
        std::string style = properties["style"].value<std::string>();
        std::string colors = properties["colors"].value<std::string>();
        std::string season = properties["season"].value<std::string>();
        std::string item_type = properties["item_type"].value<std::string>();
        int count = properties["count"].value<int>();
        
        ESP_LOGI(TAG, "Performing outfit search: style='%s', colors='%s', season='%s', item_type='%s', count=%d", 
                style.c_str(), colors.c_str(), season.c_str(), item_type.c_str(), count);
        
        // 构建服装搜索请求体
        std::string search_payload = BuildOutfitSearchPayload(style, colors, season, item_type, count);
        
        // 执行 HTTP POST 请求
        std::string json_content = HttpPost(search_payload);
        
        if (json_content.empty()) {
            ESP_LOGE(TAG, "Failed to get outfit search results");
            return "{\"status\": \"error\", \"message\": \"无法获取服装搜索结果，请检查网络连接\"}";
        }
        
        // 解析服装搜索结果
        std::string results = ParseOutfitSearchResults(json_content);
        
        ESP_LOGI(TAG, "Outfit search completed successfully");
        return results;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Outfit search failed: %s", e.what());
        std::string error_msg = "{\"status\": \"error\", \"message\": \"服装搜索失败: ";
        error_msg += e.what();
        error_msg += "\"}";
        return error_msg;
    }
}

std::string BochaSearch::ParseOutfitSearchResults(const std::string& json_content) {
    ESP_LOGD(TAG, "Parsing outfit search JSON response (%zu bytes)", json_content.length());
    
    // 使用基础的解析函数，然后增强结果
    std::string basic_results = ParseSearchResults(json_content);
    
    // 解析基础结果
    cJSON* basic_json = cJSON_Parse(basic_results.c_str());
    if (!basic_json) {
        return basic_results; // 如果解析失败，返回原始结果
    }
    
    auto status = cJSON_GetObjectItem(basic_json, "status");
    if (!status || !cJSON_IsString(status) || strcmp(status->valuestring, "success") != 0) {
        cJSON_Delete(basic_json);
        return basic_results; // 如果不成功，返回原始结果
    }
    
    // 创建增强的结果对象
    cJSON* enhanced_result = cJSON_CreateObject();
    cJSON_AddStringToObject(enhanced_result, "status", "success");
    cJSON_AddStringToObject(enhanced_result, "search_type", "outfit_recommendation");
    cJSON_AddStringToObject(enhanced_result, "search_engine", "Bocha AI");
    
    // 复制基础信息
    auto total_results = cJSON_GetObjectItem(basic_json, "total_results");
    if (total_results && cJSON_IsNumber(total_results)) {
        cJSON_AddNumberToObject(enhanced_result, "total_results", total_results->valueint);
    }
    
    auto query = cJSON_GetObjectItem(basic_json, "query");
    if (query && cJSON_IsString(query)) {
        cJSON_AddStringToObject(enhanced_result, "query", query->valuestring);
    }
    
    // 处理搜索结果并增强服装相关信息
    auto results = cJSON_GetObjectItem(basic_json, "results");
    if (results && cJSON_IsArray(results)) {
        cJSON* enhanced_results = cJSON_CreateArray();
        
        int count = cJSON_GetArraySize(results);
        for (int i = 0; i < count; i++) {
            cJSON* item = cJSON_GetArrayItem(results, i);
            if (!cJSON_IsObject(item)) continue;
            
            cJSON* enhanced_item = cJSON_CreateObject();
            
            // 复制基础字段
            auto title = cJSON_GetObjectItem(item, "title");
            if (cJSON_IsString(title)) {
                cJSON_AddStringToObject(enhanced_item, "title", title->valuestring);
            }
            
            auto link = cJSON_GetObjectItem(item, "link");
            if (cJSON_IsString(link)) {
                cJSON_AddStringToObject(enhanced_item, "link", link->valuestring);
            }
            
            auto snippet = cJSON_GetObjectItem(item, "snippet");
            if (cJSON_IsString(snippet)) {
                cJSON_AddStringToObject(enhanced_item, "snippet", snippet->valuestring);
                
                // 从片段中提取服装相关信息
                std::string snippet_text = snippet->valuestring;
                
                // 提取价格信息
                if (snippet_text.find("¥") != std::string::npos) {
                    size_t price_start = snippet_text.find("¥");
                    size_t price_end = snippet_text.find(" ", price_start);
                    if (price_end != std::string::npos) {
                        std::string price = snippet_text.substr(price_start, price_end - price_start);
                        cJSON_AddStringToObject(enhanced_item, "price", price.c_str());
                    }
                } else if (snippet_text.find("元") != std::string::npos) {
                    // 处理"元"的价格格式
                    size_t yuan_pos = snippet_text.find("元");
                    if (yuan_pos > 0) {
                        size_t price_start = yuan_pos - 1;
                        while (price_start > 0 && (isdigit(snippet_text[price_start - 1]) || snippet_text[price_start - 1] == '.')) {
                            price_start--;
                        }
                        std::string price = snippet_text.substr(price_start, yuan_pos - price_start + 1);
                        cJSON_AddStringToObject(enhanced_item, "price", price.c_str());
                    }
                }
                
                // 检测是否包含品牌信息
                std::vector<std::string> brands = {"优衣库", "ZARA", "H&M", "Nike", "Adidas", "无印良品", "海澜之家"};
                for (const auto& brand : brands) {
                    if (snippet_text.find(brand) != std::string::npos) {
                        cJSON_AddStringToObject(enhanced_item, "brand", brand.c_str());
                        break;
                    }
                }
                
                // 检测服装类型
                std::vector<std::string> types = {"T恤", "衬衫", "裤子", "裙子", "外套", "鞋子", "帽子", "包包"};
                for (const auto& type : types) {
                    if (snippet_text.find(type) != std::string::npos) {
                        cJSON_AddStringToObject(enhanced_item, "clothing_type", type.c_str());
                        break;
                    }
                }
            }
            
            auto siteName = cJSON_GetObjectItem(item, "siteName");
            if (cJSON_IsString(siteName)) {
                cJSON_AddStringToObject(enhanced_item, "siteName", siteName->valuestring);
            }
            
            // 添加推荐评分（基于标题和片段的关键词匹配）
            int relevance_score = 0;
            if (title && cJSON_IsString(title)) {
                std::string title_text = title->valuestring;
                if (title_text.find("时尚") != std::string::npos) relevance_score += 2;
                if (title_text.find("搭配") != std::string::npos) relevance_score += 2;
                if (title_text.find("推荐") != std::string::npos) relevance_score += 1;
            }
            cJSON_AddNumberToObject(enhanced_item, "relevance_score", relevance_score);
            
            cJSON_AddItemToArray(enhanced_results, enhanced_item);
        }
        
        cJSON_AddItemToObject(enhanced_result, "results", enhanced_results);
    }
    
    // 添加服装搜索特有的建议
    cJSON_AddStringToObject(enhanced_result, "shopping_advice", "建议对比多个商品的价格、质量和用户评价再购买");
    cJSON_AddStringToObject(enhanced_result, "style_tip", "选择服装时要考虑与现有衣物的搭配协调性");
    
    // 转换为字符串
    char* json_str = cJSON_PrintUnformatted(enhanced_result);
    std::string result_str(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(enhanced_result);
    cJSON_Delete(basic_json);
    
    return result_str;
}
