/*
 * Bocha AI Search Tool Header
 * 提供基于 MCP 协议的 Bocha AI 搜索功能
 */

#ifndef BOCHA_SEARCH_H
#define BOCHA_SEARCH_H

#include "mcp_server.h"
#include <string>
#include <esp_http_client.h>

class BochaSearch {
public:
    // 注册 Bocha 搜索工具到 MCP 服务器
    static void RegisterTools();
    
    // URL 编码
    static std::string UrlEncode(const std::string& str);
    
    // 构建搜索请求体
    static std::string BuildSearchPayload(const std::string& query, int count, bool summary = true);
    
    // HTTP 事件处理器
    static esp_err_t HttpEventHandler(esp_http_client_event_t *evt);
    
    // 执行 HTTP POST 请求
    static std::string HttpPost(const std::string& payload);
    
    // 解析搜索结果
    static std::string ParseSearchResults(const std::string& json_content);
    
    // 执行搜索的回调函数
    static ReturnValue DoSearch(const PropertyList& properties);
    
    // 专门的服装推荐搜索
    static ReturnValue DoOutfitSearch(const PropertyList& properties);
    
    // 构建专门的服装搜索请求体
    static std::string BuildOutfitSearchPayload(const std::string& style, const std::string& colors, 
                                                const std::string& season, const std::string& item_type, int count);
    
    // 解析服装搜索结果（增强版）
    static std::string ParseOutfitSearchResults(const std::string& json_content);
};

#endif // BOCHA_SEARCH_H
