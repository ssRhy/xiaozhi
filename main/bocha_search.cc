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
#define MAX_RESPONSE_SIZE 16384
#define HTTP_TIMEOUT_MS 30000  // 30 秒超时
#define MAX_RETRIES 3          // 最大重试次数
#define RETRY_DELAY_MS 1000    // 重试间隔

void BochaSearch::RegisterTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    mcp_server.AddTool(
        "self.search.bocha",
        "使用 Bocha AI 进行网络搜索，获取准确和最新的信息。\n"
        "参数说明:\n"
        "- query: 搜索关键词(必填)\n"
        "- count: 返回结果数量限制(1-20, 默认10)\n"
        "使用场景:\n"
        "1. 搜索网络信息和资料\n"
        "2. 获取实时资讯和技术文档\n"
        "3. 查找特定主题的详细信息\n"
        "4. 获取最新新闻和时事",
        PropertyList({
            Property("query", kPropertyTypeString),  // 必填参数
            Property("count", kPropertyTypeInteger, 10, 1, 20)  // 可选参数，默认值10，范围1-20
        }),
        DoSearch
    );

    ESP_LOGI(TAG, "Bocha AI search tool registered successfully");
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
    
    cJSON_AddStringToObject(payload, "query", query.c_str());
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
        
        esp_http_client_config_t config = {};
        config.url = BOCHA_API_URL;
        config.method = HTTP_METHOD_POST;
        config.timeout_ms = HTTP_TIMEOUT_MS;
        config.buffer_size = MAX_RESPONSE_SIZE;
        config.user_agent = "XiaoZhi-ESP32/1.0";
        config.transport_type = HTTP_TRANSPORT_OVER_SSL;
        config.skip_cert_common_name_check = true;
        
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
        
        std::string response;
        esp_err_t err = esp_http_client_perform(client);
        
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            int content_length = esp_http_client_get_content_length(client);
            
            ESP_LOGI(TAG, "HTTP Status: %d, Content Length: %d", status_code, content_length);
            
            if (status_code == 200 && content_length > 0) {
                // 读取响应内容
                int actual_length = std::min(content_length, MAX_RESPONSE_SIZE - 1);
                std::vector<char> buffer(actual_length + 1);
                
                int read_len = esp_http_client_read(client, buffer.data(), actual_length);
                if (read_len > 0) {
                    buffer[read_len] = '\0';
                    response = buffer.data();
                    ESP_LOGI(TAG, "Successfully received response from Bocha AI");
                    esp_http_client_cleanup(client);
                    return response;
                } else {
                    ESP_LOGE(TAG, "Failed to read response data: %d", read_len);
                }
            } else {
                ESP_LOGW(TAG, "HTTP request failed with status: %d", status_code);
                // 尝试读取错误响应
                char buf[512] = {0};
                int read_len = esp_http_client_read(client, buf, sizeof(buf) - 1);
                if (read_len > 0) {
                    ESP_LOGW(TAG, "Error response: %s", buf);
                }
            }
        } else {
            ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
            int err_code = esp_http_client_get_errno(client);
            ESP_LOGE(TAG, "HTTP error code: %d", err_code);
        }
        
        esp_http_client_cleanup(client);
    }
    
    ESP_LOGE(TAG, "All retry attempts failed");
    return "";
}

std::string BochaSearch::ParseSearchResults(const std::string& json_content) {
    // 解析 Bocha AI API 返回的 JSON
    cJSON* bocha_json = cJSON_Parse(json_content.c_str());
    if (!bocha_json) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
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
