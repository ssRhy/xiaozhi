/*
 * Outfit Analyzer Implementation
 * 专属个人衣服穿搭分析和推荐系统实现
 */

#include "outfit_analyzer.h"
#include "bocha_search.h"
#include "board.h"
#include <esp_log.h>
#include <cJSON.h>
#include <algorithm>
#include <sstream>
#include <variant>

#define TAG "OUTFIT_ANALYZER"

OutfitAnalyzer& OutfitAnalyzer::GetInstance() {
    static OutfitAnalyzer instance;
    return instance;
}

void OutfitAnalyzer::RegisterTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 穿搭分析工具
    mcp_server.AddTool(
        "self.outfit.analyze",
        "拍照分析当前穿搭风格、颜色搭配和服装单品。\n"
        "使用场景：\n"
        "1. 用户想知道当前穿搭如何\n"
        "2. 需要穿搭建议和评价\n"
        "3. 想了解自己的穿搭风格\n"
        "返回：详细的穿搭分析报告",
        PropertyList(),
        DoOutfitAnalysis
    );
    
    // 基于分析的推荐工具
    mcp_server.AddTool(
        "self.outfit.recommend",
        "基于当前穿搭分析结果，搜索并推荐配套的衣物单品。\n"
        "参数说明:\n"
        "- analysis_result: 穿搭分析结果(可选，如为空会先进行拍照分析)\n"
        "使用场景：\n"
        "1. 想要寻找配套的衣物\n"
        "2. 需要完善当前穿搭\n"
        "3. 寻找类似风格的服装\n"
        "返回：推荐的衣物列表和购买链接",
        PropertyList({
            Property("analysis_result", kPropertyTypeString, "")  // 可选参数
        }),
        DoOutfitRecommendation
    );
    
    // 完整的穿搭服务（分析+推荐）
    mcp_server.AddTool(
        "self.outfit.complete_service",
        "提供完整的穿搭服务：拍照分析当前穿搭并推荐配套衣物。\n"
        "这是最常用的功能，一次性完成穿搭分析和推荐。\n"
        "使用场景：\n"
        "1. 用户说'帮我看看穿搭'或'推荐一些衣服'\n"
        "2. 需要完整的穿搭建议\n"
        "3. 想要购买配套衣物\n"
        "返回：穿搭分析 + 推荐衣物的完整报告",
        PropertyList(),
        DoCompleteOutfitService
    );

    ESP_LOGI(TAG, "Outfit analyzer tools registered successfully");
}

void OutfitAnalyzer::SetCamera(Camera* camera) {
    camera_ = camera;
}

OutfitAnalysis OutfitAnalyzer::AnalyzeCurrentOutfit() {
    OutfitAnalysis result;
    result.success = false;
    
    if (!camera_) {
        result.message = "相机未初始化";
        return result;
    }
    
    ESP_LOGI(TAG, "Starting outfit analysis...");
    
    // 拍照
    if (!camera_->Capture()) {
        result.message = "拍照失败，请检查相机";
        return result;
    }
    
    // 构建专门用于穿搭分析的问题
    std::string outfit_question = 
        "请详细分析这张照片中的穿搭，包括：\n"
        "1. 整体风格（如休闲、正式、运动、时尚等）\n"
        "2. 颜色搭配方案\n"
        "3. 具体的服装单品（如上衣、裤子、鞋子、配饰等）\n"
        "4. 适合的季节和场合\n"
        "5. 可以补充的配套单品建议\n"
        "6. 整体搭配评价和改进建议\n"
        "请用JSON格式回答，包含style, colors, items, season, occasion, suggestions等字段。";
    
    // 发送到相机AI分析
    std::string camera_response = camera_->Explain(outfit_question);
    
    // 解析相机分析结果
    result = ParseCameraAnalysis(camera_response);
    
    if (result.success) {
        ESP_LOGI(TAG, "Outfit analysis completed successfully");
        ESP_LOGI(TAG, "Style: %s", result.overall_style.c_str());
        ESP_LOGI(TAG, "Colors: %s", result.color_scheme.c_str());
    } else {
        ESP_LOGE(TAG, "Outfit analysis failed: %s", result.message.c_str());
    }
    
    return result;
}

OutfitRecommendation OutfitAnalyzer::RecommendOutfitItems(const OutfitAnalysis& analysis) {
    OutfitRecommendation result;
    result.success = false;
    
    if (!analysis.success) {
        result.message = "需要先进行穿搭分析";
        return result;
    }
    
    ESP_LOGI(TAG, "Starting outfit recommendation based on analysis...");
    
    // 构建搜索查询
    std::string search_query = BuildRecommendationQuery(analysis);
    ESP_LOGI(TAG, "Recommendation search query: %s", search_query.c_str());
    
    // 使用Bocha搜索推荐衣物
    PropertyList search_properties({
        Property("query", kPropertyTypeString, search_query),
        Property("count", kPropertyTypeInteger, 4)
    });
    
    ReturnValue search_result = BochaSearch::DoSearch(search_properties);
    std::string search_response;
    if (std::holds_alternative<std::string>(search_result)) {
        search_response = std::get<std::string>(search_result);
    } else {
        result.message = "搜索返回类型错误";
        return result;
    }
    
    // 解析搜索结果为推荐结果
    result = ParseSearchResults(search_response, analysis);
    
    if (result.success) {
        ESP_LOGI(TAG, "Outfit recommendation completed successfully");
        ESP_LOGI(TAG, "Found %d recommended items", result.items.size());
    } else {
        ESP_LOGE(TAG, "Outfit recommendation failed: %s", result.message.c_str());
    }
    
    return result;
}

std::string OutfitAnalyzer::AnalyzeAndRecommend() {
    ESP_LOGI(TAG, "Starting complete outfit analysis and recommendation service");
    
    // 步骤1：分析当前穿搭
    OutfitAnalysis analysis = AnalyzeCurrentOutfit();
    if (!analysis.success) {
        return "{\"success\": false, \"message\": \"" + analysis.message + "\"}";
    }
    
    // 步骤2：基于分析结果推荐衣物
    OutfitRecommendation recommendation = RecommendOutfitItems(analysis);
    
    // 构建完整的JSON响应
    cJSON* response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "service", "complete_outfit_analysis");
    
    // 添加分析结果
    cJSON* analysis_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(analysis_obj, "overall_style", analysis.overall_style.c_str());
    cJSON_AddStringToObject(analysis_obj, "color_scheme", analysis.color_scheme.c_str());
    cJSON_AddStringToObject(analysis_obj, "season", analysis.season.c_str());
    cJSON_AddStringToObject(analysis_obj, "occasion", analysis.occasion.c_str());
    
    // 添加识别到的服装单品
    cJSON* items_array = cJSON_CreateArray();
    for (const auto& item : analysis.items) {
        cJSON_AddItemToArray(items_array, cJSON_CreateString(item.c_str()));
    }
    cJSON_AddItemToObject(analysis_obj, "current_items", items_array);
    
    // 添加缺失的单品建议
    cJSON* missing_items_array = cJSON_CreateArray();
    for (const auto& item : analysis.missing_items) {
        cJSON_AddItemToArray(missing_items_array, cJSON_CreateString(item.c_str()));
    }
    cJSON_AddItemToObject(analysis_obj, "missing_items", missing_items_array);
    
    cJSON_AddItemToObject(response, "analysis", analysis_obj);
    
    // 添加推荐结果
    if (recommendation.success) {
        cJSON* recommendation_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(recommendation_obj, "style_advice", recommendation.style_advice.c_str());
        cJSON_AddStringToObject(recommendation_obj, "color_advice", recommendation.color_advice.c_str());
        
        // 添加推荐的衣物
        cJSON* recommended_items_array = cJSON_CreateArray();
        for (const auto& item : recommendation.items) {
            cJSON* item_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(item_obj, "title", item.title.c_str());
            cJSON_AddStringToObject(item_obj, "link", item.link.c_str());
            cJSON_AddStringToObject(item_obj, "snippet", item.snippet.c_str());
            cJSON_AddStringToObject(item_obj, "site_name", item.site_name.c_str());
            if (!item.price_info.empty()) {
                cJSON_AddStringToObject(item_obj, "price_info", item.price_info.c_str());
            }
            cJSON_AddItemToArray(recommended_items_array, item_obj);
        }
        cJSON_AddItemToObject(recommendation_obj, "recommended_items", recommended_items_array);
        
        cJSON_AddItemToObject(response, "recommendations", recommendation_obj);
    } else {
        cJSON_AddStringToObject(response, "recommendation_error", recommendation.message.c_str());
    }
    
    // 生成最终的风格建议
    std::string style_advice = GenerateStyleAdvice(analysis);
    cJSON_AddStringToObject(response, "style_advice", style_advice.c_str());
    
    char* json_str = cJSON_PrintUnformatted(response);
    std::string result_str(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(response);
    
    ESP_LOGI(TAG, "Complete outfit service finished successfully");
    return result_str;
}

// MCP工具实现函数
ReturnValue OutfitAnalyzer::DoOutfitAnalysis(const PropertyList& properties) {
    auto& analyzer = GetInstance();
    OutfitAnalysis result = analyzer.AnalyzeCurrentOutfit();
    
    if (!result.success) {
        return "{\"success\": false, \"message\": \"" + result.message + "\"}";
    }
    
    // 构建分析结果JSON
    cJSON* response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "overall_style", result.overall_style.c_str());
    cJSON_AddStringToObject(response, "color_scheme", result.color_scheme.c_str());
    cJSON_AddStringToObject(response, "season", result.season.c_str());
    cJSON_AddStringToObject(response, "occasion", result.occasion.c_str());
    
    // 添加服装单品数组
    cJSON* items_array = cJSON_CreateArray();
    for (const auto& item : result.items) {
        cJSON_AddItemToArray(items_array, cJSON_CreateString(item.c_str()));
    }
    cJSON_AddItemToObject(response, "items", items_array);
    
    // 添加建议数组
    cJSON* suggestions_array = cJSON_CreateArray();
    for (const auto& suggestion : result.style_suggestions) {
        cJSON_AddItemToArray(suggestions_array, cJSON_CreateString(suggestion.c_str()));
    }
    cJSON_AddItemToObject(response, "style_suggestions", suggestions_array);
    
    char* json_str = cJSON_PrintUnformatted(response);
    std::string result_str(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(response);
    
    return result_str;
}

ReturnValue OutfitAnalyzer::DoOutfitRecommendation(const PropertyList& properties) {
    auto& analyzer = GetInstance();
    
    // 检查是否提供了分析结果，如果没有则先进行分析
    std::string analysis_result = properties["analysis_result"].value<std::string>();
    
    OutfitAnalysis analysis;
    if (analysis_result.empty()) {
        analysis = analyzer.AnalyzeCurrentOutfit();
        if (!analysis.success) {
            return "{\"success\": false, \"message\": \"" + analysis.message + "\"}";
        }
    } else {
        // 如果提供了分析结果，可以解析使用（这里简化处理）
        analysis = analyzer.AnalyzeCurrentOutfit();
        if (!analysis.success) {
            return "{\"success\": false, \"message\": \"需要先进行穿搭分析\"}";
        }
    }
    
    OutfitRecommendation recommendation = analyzer.RecommendOutfitItems(analysis);
    
    if (!recommendation.success) {
        return "{\"success\": false, \"message\": \"" + recommendation.message + "\"}";
    }
    
    // 构建推荐结果JSON
    cJSON* response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "style_advice", recommendation.style_advice.c_str());
    cJSON_AddStringToObject(response, "color_advice", recommendation.color_advice.c_str());
    
    cJSON* items_array = cJSON_CreateArray();
    for (const auto& item : recommendation.items) {
        cJSON* item_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(item_obj, "title", item.title.c_str());
        cJSON_AddStringToObject(item_obj, "link", item.link.c_str());
        cJSON_AddStringToObject(item_obj, "snippet", item.snippet.c_str());
        cJSON_AddStringToObject(item_obj, "site_name", item.site_name.c_str());
        if (!item.price_info.empty()) {
            cJSON_AddStringToObject(item_obj, "price_info", item.price_info.c_str());
        }
        cJSON_AddItemToArray(items_array, item_obj);
    }
    cJSON_AddItemToObject(response, "recommended_items", items_array);
    
    char* json_str = cJSON_PrintUnformatted(response);
    std::string result_str(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(response);
    
    return result_str;
}

ReturnValue OutfitAnalyzer::DoCompleteOutfitService(const PropertyList& properties) {
    auto& analyzer = GetInstance();
    return analyzer.AnalyzeAndRecommend();
}

// 内部辅助函数实现
OutfitAnalysis OutfitAnalyzer::ParseCameraAnalysis(const std::string& camera_response) {
    OutfitAnalysis result;
    result.success = false;
    
    ESP_LOGD(TAG, "Parsing camera analysis response");
    
    // 解析相机返回的JSON
    cJSON* camera_json = cJSON_Parse(camera_response.c_str());
    if (!camera_json) {
        result.message = "相机分析结果解析失败";
        return result;
    }
    
    auto success = cJSON_GetObjectItem(camera_json, "success");
    if (!success || !cJSON_IsBool(success) || !cJSON_IsTrue(success)) {
        auto msg = cJSON_GetObjectItem(camera_json, "message");
        result.message = (msg && cJSON_IsString(msg)) ? msg->valuestring : "相机分析失败";
        cJSON_Delete(camera_json);
        return result;
    }
    
    // 提取AI分析的文本结果 - 检查 "text" 字段
    auto ai_result = cJSON_GetObjectItem(camera_json, "text");
    if (!ai_result || !cJSON_IsString(ai_result)) {
        // 如果没有 "text" 字段，尝试 "result" 字段（向后兼容）
        ai_result = cJSON_GetObjectItem(camera_json, "result");
        if (!ai_result || !cJSON_IsString(ai_result)) {
            result.message = "未获取到有效的分析结果";
            cJSON_Delete(camera_json);
            return result;
        }
    }
    
    std::string ai_text = ai_result->valuestring;
    ESP_LOGI(TAG, "AI analysis result: %s", ai_text.c_str());
    
    // 尝试解析AI返回的JSON结构
    cJSON* ai_json = cJSON_Parse(ai_text.c_str());
    if (ai_json) {
        // AI返回了结构化的JSON，直接提取字段
        result.success = true;
        result.message = "穿搭分析完成";
        
        auto style = cJSON_GetObjectItem(ai_json, "style");
        if (style && cJSON_IsString(style)) {
            result.overall_style = style->valuestring;
        }
        
        auto colors = cJSON_GetObjectItem(ai_json, "colors");
        if (colors && cJSON_IsString(colors)) {
            result.color_scheme = colors->valuestring;
        }
        
        auto items = cJSON_GetObjectItem(ai_json, "items");
        if (items && cJSON_IsArray(items)) {
            int count = cJSON_GetArraySize(items);
            for (int i = 0; i < count; i++) {
                cJSON* item = cJSON_GetArrayItem(items, i);
                if (cJSON_IsString(item)) {
                    result.items.push_back(item->valuestring);
                }
            }
        }
        
        auto season = cJSON_GetObjectItem(ai_json, "season");
        if (season && cJSON_IsString(season)) {
            result.season = season->valuestring;
        }
        
        auto occasion = cJSON_GetObjectItem(ai_json, "occasion");
        if (occasion && cJSON_IsString(occasion)) {
            result.occasion = occasion->valuestring;
        }
        
        auto suggestions = cJSON_GetObjectItem(ai_json, "suggestions");
        if (suggestions && cJSON_IsString(suggestions)) {
            result.style_suggestions.push_back(suggestions->valuestring);
        }
        
        // 生成搜索关键词
        result.search_keywords = result.overall_style + " " + result.color_scheme + " 服装搭配";
        
        // 根据风格生成配套建议
        if (result.overall_style.find("休闲") != std::string::npos) {
            result.missing_items.push_back("休闲鞋");
            result.missing_items.push_back("牛仔外套");
        } else if (result.overall_style.find("正式") != std::string::npos) {
            result.missing_items.push_back("正装鞋");
            result.missing_items.push_back("领带");
        } else if (result.overall_style.find("时尚") != std::string::npos) {
            result.missing_items.push_back("时尚配饰");
            result.missing_items.push_back("潮流单品");
        }
        
        cJSON_Delete(ai_json);
    } else {
        // AI返回的不是JSON格式，使用文本解析
        result.success = true;
        result.message = "穿搭分析完成";
        
        // 提取风格信息
        if (ai_text.find("休闲") != std::string::npos) {
            result.overall_style = "休闲";
        } else if (ai_text.find("正式") != std::string::npos) {
            result.overall_style = "正式";
        } else if (ai_text.find("运动") != std::string::npos) {
            result.overall_style = "运动";
        } else if (ai_text.find("时尚") != std::string::npos) {
            result.overall_style = "时尚";
        } else {
            result.overall_style = "混搭";
        }
        
        // 提取颜色信息
        std::vector<std::string> colors = {"黑色", "白色", "蓝色", "红色", "灰色", "绿色", "黄色", "粉色", "紫色", "棕色"};
        for (const auto& color : colors) {
            if (ai_text.find(color) != std::string::npos) {
                if (!result.color_scheme.empty()) result.color_scheme += "+";
                result.color_scheme += color;
            }
        }
        if (result.color_scheme.empty()) {
            result.color_scheme = "多色搭配";
        }
        
        // 季节和场合的简单解析
        if (ai_text.find("夏") != std::string::npos) {
            result.season = "夏季";
        } else if (ai_text.find("春") != std::string::npos) {
            result.season = "春季";
        } else {
            result.season = "四季";
        }
        
        result.occasion = "日常";
        result.search_keywords = result.overall_style + " " + result.color_scheme + " 服装搭配";
    }
    
    cJSON_Delete(camera_json);
    return result;
}

std::string OutfitAnalyzer::BuildRecommendationQuery(const OutfitAnalysis& analysis) {
    std::stringstream query;
    
    // 基于风格构建查询
    query << analysis.overall_style << "风格 ";
    
    // 添加颜色信息
    if (!analysis.color_scheme.empty()) {
        query << analysis.color_scheme << " ";
    }
    
    // 添加季节信息
    if (!analysis.season.empty()) {
        query << analysis.season << " ";
    }
    
    // 添加推荐的缺失单品
    if (!analysis.missing_items.empty()) {
        query << analysis.missing_items[0] << " ";
    }
    
    // 添加基础搜索词
    query << "服装 穿搭 搭配 推荐 时尚";
    
    // 如果是特定场合，添加场合关键词
    if (!analysis.occasion.empty() && analysis.occasion != "日常") {
        query << " " << analysis.occasion;
    }
    
    return query.str();
}

OutfitRecommendation OutfitAnalyzer::ParseSearchResults(const std::string& search_response, const OutfitAnalysis& analysis) {
    OutfitRecommendation result;
    result.success = false;
    
    ESP_LOGD(TAG, "Parsing search results for outfit recommendations");
    
    // 解析搜索结果JSON
    cJSON* search_json = cJSON_Parse(search_response.c_str());
    if (!search_json) {
        result.message = "搜索结果解析失败";
        return result;
    }
    
    auto status = cJSON_GetObjectItem(search_json, "status");
    if (!status || !cJSON_IsString(status) || strcmp(status->valuestring, "success") != 0) {
        auto msg = cJSON_GetObjectItem(search_json, "message");
        result.message = (msg && cJSON_IsString(msg)) ? msg->valuestring : "搜索失败";
        cJSON_Delete(search_json);
        return result;
    }
    
    auto results = cJSON_GetObjectItem(search_json, "results");
    if (!results || !cJSON_IsArray(results)) {
        result.message = "未找到推荐结果";
        cJSON_Delete(search_json);
        return result;
    }
    
    // 解析搜索结果为推荐项目
    int count = cJSON_GetArraySize(results);
    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(results, i);
        if (!cJSON_IsObject(item)) continue;
        
        RecommendedItem rec_item;
        
        auto title = cJSON_GetObjectItem(item, "title");
        if (cJSON_IsString(title)) {
            rec_item.title = title->valuestring;
        }
        
        auto link = cJSON_GetObjectItem(item, "link");
        if (cJSON_IsString(link)) {
            rec_item.link = link->valuestring;
        }
        
        auto snippet = cJSON_GetObjectItem(item, "snippet");
        if (cJSON_IsString(snippet)) {
            rec_item.snippet = snippet->valuestring;
        }
        
        auto siteName = cJSON_GetObjectItem(item, "siteName");
        if (cJSON_IsString(siteName)) {
            rec_item.site_name = siteName->valuestring;
        }
        
        // 尝试从片段中提取价格信息
        if (!rec_item.snippet.empty()) {
            if (rec_item.snippet.find("¥") != std::string::npos || 
                rec_item.snippet.find("元") != std::string::npos ||
                rec_item.snippet.find("价格") != std::string::npos) {
                size_t price_pos = rec_item.snippet.find("¥");
                if (price_pos != std::string::npos) {
                    size_t end_pos = rec_item.snippet.find(" ", price_pos);
                    if (end_pos != std::string::npos) {
                        rec_item.price_info = rec_item.snippet.substr(price_pos, end_pos - price_pos);
                    }
                }
            }
        }
        
        result.items.push_back(rec_item);
    }
    
    result.success = true;
    result.style_advice = GenerateStyleAdvice(analysis);
    result.color_advice = "建议保持与当前 " + analysis.color_scheme + " 的协调搭配";
    
    cJSON_Delete(search_json);
    return result;
}

std::string OutfitAnalyzer::GenerateStyleAdvice(const OutfitAnalysis& analysis) {
    std::stringstream advice;
    
    advice << "基于您当前的" << analysis.overall_style << "风格穿搭分析：\n";
    
    if (!analysis.color_scheme.empty()) {
        advice << "您的" << analysis.color_scheme << "搭配很不错。";
    }
    
    if (!analysis.missing_items.empty()) {
        advice << "建议添加";
        for (size_t i = 0; i < analysis.missing_items.size(); ++i) {
            if (i > 0) advice << "、";
            advice << analysis.missing_items[i];
        }
        advice << "来完善整体造型。";
    }
    
    // 根据风格给出具体建议
    if (analysis.overall_style == "休闲") {
        advice << "休闲风格适合大多数日常场合，可以尝试叠搭增加层次感。";
    } else if (analysis.overall_style == "正式") {
        advice << "正式风格很适合商务场合，注意配色的统一性。";
    } else if (analysis.overall_style == "运动") {
        advice << "运动风格舒适实用，可以加一些运动配饰。";
    }
    
    return advice.str();
}
