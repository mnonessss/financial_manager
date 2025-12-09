#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpBinder.h>
#include <drogon/orm/CoroMapper.h>
#include "models/Category.h"

namespace finance {

class CategoryController : public drogon::HttpController<CategoryController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(CategoryController::CreateCategory, "/categories", drogon::Post);
        ADD_METHOD_TO(CategoryController::GetCategories, "/categories", drogon::Get);
        ADD_METHOD_TO(CategoryController::UpdateCategory, "/categories/{categoryId}", drogon::Put);
        ADD_METHOD_TO(CategoryController::DeleteCategory, "/categories/{categoryId}", drogon::Delete);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> CreateCategory(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> GetCategories(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> UpdateCategory(drogon::HttpRequestPtr req, int categoryId);
    drogon::Task<drogon::HttpResponsePtr> DeleteCategory(drogon::HttpRequestPtr req, int categoryId);
};

}


