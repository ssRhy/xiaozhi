# DuckDuckGo 搜索工具使用说明

## 概述

本项目已成功集成 DuckDuckGo 搜索功能作为 MCP 工具，可以通过标准 JSON-RPC 2.0 协议调用。

## 工具信息

- **工具名称**: `self.search.duckduckgo`
- **功能**: 使用 DuckDuckGo 进行网络搜索
- **支持功能**: 通用搜索、站内搜索、结果数量控制

## 参数说明

| 参数名 | 类型 | 是否必填 | 默认值 | 范围 | 说明 |
|--------|------|----------|--------|------|------|
| `query` | string | 是 | - | - | 搜索关键词 |
| `max_results` | integer | 否 | 5 | 1-10 | 返回结果数量限制 |
| `site` | string | 否 | "" | - | 限制在特定网站内搜索 |

## 使用示例

### 1. 基本搜索

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.search.duckduckgo",
    "arguments": {
      "query": "ESP32 开发教程"
    }
  },
  "id": 1
}
```

### 2. 限制结果数量的搜索

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.search.duckduckgo",
    "arguments": {
      "query": "物联网 IoT",
      "max_results": 3
    }
  },
  "id": 2
}
```

### 3. 站内搜索

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.search.duckduckgo",
    "arguments": {
      "query": "ESP-IDF API",
      "site": "docs.espressif.com",
      "max_results": 5
    }
  },
  "id": 3
}
```

### 4. 获取工具列表（验证工具是否注册成功）

```json
{
  "jsonrpc": "2.0",
  "method": "tools/list",
  "params": {
    "cursor": ""
  },
  "id": 4
}
```

## 返回格式

成功调用时返回：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"status\":\"success\",\"total_results\":5,\"search_engine\":\"DuckDuckGo\",\"results\":[...]}"
      }
    ],
    "isError": false
  }
}
```

错误时返回：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32601,
    "message": "搜索失败: 网络连接错误"
  }
}
```

## 技术实现细节

### 文件结构

- `main/duckduckgo_search.h` - 头文件定义
- `main/duckduckgo_search.cc` - 实现文件
- `main/mcp_server.cc` - 工具注册位置

### 依赖组件

- `esp_http_client` - HTTP 客户端功能
- `json` - JSON 解析和生成
- `cJSON` - C JSON 库

### 关键特性

1. **网络请求**: 使用 ESP-IDF 的 HTTP 客户端
2. **URL 编码**: 自动处理搜索关键词的 URL 编码
3. **错误处理**: 完善的错误处理和日志记录
4. **超时控制**: 10秒 HTTP 请求超时
5. **结果限制**: 最大响应大小 8KB，防止内存溢出

### 配置说明

在 `main/CMakeLists.txt` 中已添加必要依赖：

```cmake
REQUIRES esp_http_client json
```

## 使用场景

1. **信息查询**: 搜索技术文档、教程等
2. **实时资讯**: 获取最新新闻和信息
3. **站内搜索**: 在特定网站内搜索内容
4. **技术支持**: 搜索问题解决方案

## 注意事项

1. 需要设备连接到互联网
2. 搜索结果受 DuckDuckGo 服务可用性影响
3. 当前版本提供简化的 HTML 解析，实际项目可能需要更完善的解析器
4. 遵守 DuckDuckGo 的使用条款和频率限制

## 开发和调试

可以通过 ESP32 串口监控查看详细日志：

```
I (xxxxx) DDG_SEARCH: Making HTTP request to: https://duckduckgo.com/?q=ESP32...
I (xxxxx) DDG_SEARCH: HTTP Status: 200, Content Length: 1234
I (xxxxx) DDG_SEARCH: Search completed successfully
```
