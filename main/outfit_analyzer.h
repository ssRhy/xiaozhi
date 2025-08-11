/*
 * Outfit Analyzer Implementation
 * 专属个人衣服穿搭分析和推荐系统
 * 
 * 功能说明：
 * 1. 拍照分析当前穿搭
 * 2. 基于穿搭分析结果搜索推荐配套衣物
 * 3. 提供个性化服装搭配建议
 */

#ifndef OUTFIT_ANALYZER_H
#define OUTFIT_ANALYZER_H

#include <string>
#include <vector>
#include <memory>
#include "mcp_server.h"
#include "boards/common/camera.h"

// 穿搭分析结果结构
struct OutfitAnalysis {
    bool success;
    std::string message;
    
    // 分析到的服装信息
    std::string overall_style;      // 整体风格 (casual, formal, sporty, etc.)
    std::string color_scheme;       // 色彩搭配
    std::vector<std::string> items; // 识别到的服装单品
    std::string season;             // 适合的季节
    std::string occasion;           // 适合的场合
    
    // 推荐信息
    std::vector<std::string> missing_items;     // 可以补充的单品
    std::vector<std::string> style_suggestions; // 风格建议
    std::string search_keywords;                // 用于搜索推荐的关键词
};

// 服装推荐结果结构
struct OutfitRecommendation {
    bool success;
    std::string message;
    
    std::vector<struct RecommendedItem> items;
    std::string style_advice;
    std::string color_advice;
};

struct RecommendedItem {
    std::string title;
    std::string link;
    std::string snippet;
    std::string price_info;
    std::string site_name;
};

class OutfitAnalyzer {
public:
    static OutfitAnalyzer& GetInstance();
    
    // 注册MCP工具
    static void RegisterTools();
    
    // 核心功能
    OutfitAnalysis AnalyzeCurrentOutfit();
    OutfitRecommendation RecommendOutfitItems(const OutfitAnalysis& analysis);
    
    // 完整的穿搭分析和推荐流程
    std::string AnalyzeAndRecommend();
    
    // 设置相机实例
    void SetCamera(Camera* camera);
    
private:
    OutfitAnalyzer() = default;
    ~OutfitAnalyzer() = default;
    
    // 禁止拷贝
    OutfitAnalyzer(const OutfitAnalyzer&) = delete;
    OutfitAnalyzer& operator=(const OutfitAnalyzer&) = delete;
    
    // 内部工具函数
    static ReturnValue DoOutfitAnalysis(const PropertyList& properties);
    static ReturnValue DoOutfitRecommendation(const PropertyList& properties);
    static ReturnValue DoCompleteOutfitService(const PropertyList& properties);
    
    // 解析相机分析结果
    OutfitAnalysis ParseCameraAnalysis(const std::string& camera_response);
    
    // 构建推荐搜索查询
    std::string BuildRecommendationQuery(const OutfitAnalysis& analysis);
    
    // 解析搜索结果为推荐结果
    OutfitRecommendation ParseSearchResults(const std::string& search_response, const OutfitAnalysis& analysis);
    
    // 生成穿搭建议
    std::string GenerateStyleAdvice(const OutfitAnalysis& analysis);
    
    Camera* camera_ = nullptr;
};

#endif // OUTFIT_ANALYZER_H
