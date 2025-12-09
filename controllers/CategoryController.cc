#include "CategoryController.h"
#include <drogon/HttpResponse.h>
#include <drogon/orm/CoroMapper.h>
#include <jsoncpp/json/json.h>
#include <drogon/HttpAppFramework.h>
#include "utils/JwtUtils.h"

using namespace finance;
using namespace drogon_model::financial_manager;
using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
using drogon::Task;

Task<HttpResponsePtr> CategoryController::CreateCategory(HttpRequestPtr req) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        auto json = req->getJsonObject();
        if (!json) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid JSON");
            co_return resp;
        }

        if (!json->isMember("name") ||
            !json->isMember("type")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required fields: name, type");
            co_return resp;
        }

        std::string name = (*json)["name"].asString();
        std::string type = (*json)["type"].asString(); // income/expense

        if (type != "income" && type != "expense") {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid type. Must be 'income' or 'expense'");
            co_return resp;
        }

        // Семейная категория создаётся только при family=true в запросе
        bool isFamily = req->getParameter("family") == "true";
        if (isFamily) {
            // Проверяем, что пользователь состоит в семье
            auto db = drogon::app().getFastDbClient();
            auto familyCheck = co_await db->execSqlCoro(
                "SELECT id_family FROM family_members WHERE id_user = $1",
                *userIdOpt
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("User is not a member of any family");
                co_return resp;
            }
        }

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Category> mapper(db);

        Category cat;
        cat.setIdUser(static_cast<int32_t>(*userIdOpt));
        cat.setName(name);
        cat.setType(type);
        if (isFamily) {
            cat.setIsFamily(true);
        }

        auto inserted = co_await mapper.insert(cat);

        auto result = inserted.toJson();
        // Убеждаемся, что is_family правильно установлен в ответе
        auto isFamilyPtr = inserted.getIsFamily();
        if (isFamilyPtr) {
            result["is_family"] = *isFamilyPtr;
        } else {
            result["is_family"] = false;
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k201Created);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "CreateCategory error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> CategoryController::GetCategories(HttpRequestPtr req) {
    try {
        auto db = drogon::app().getFastDbClient();

        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        // Проверяем параметр family
        bool isFamily = req->getParameter("family") == "true";
        
        Json::Value arr(Json::arrayValue);
        
        if (isFamily) {
            // Получаем семейные категории (категории всех членов семьи)
            auto familyMembers = co_await db->execSqlCoro(
                R"(
                SELECT DISTINCT fm.id_user
                FROM family_members fm
                WHERE fm.id_family = (
                    SELECT fm2.id_family 
                    FROM family_members fm2 
                    WHERE fm2.id_user = $1
                )
                )", *userIdOpt
            );
            
            if (!familyMembers.empty()) {
                // Получаем семейные категории всех членов семьи (только is_family = true)
                auto familyCategories = co_await db->execSqlCoro(
                    R"(
                    SELECT c.id, c.id_user, c.name, c.type, c.is_family
                    FROM category c
                    JOIN family_members fm ON fm.id_user = c.id_user
                    WHERE fm.id_family IN (
                        SELECT fm2.id_family FROM family_members fm2 WHERE fm2.id_user = $1
                    )
                    AND c.is_family = TRUE
                    )", *userIdOpt
                );

                for (const auto &row : familyCategories) {
                    Json::Value catJson;
                    catJson["id"] = (Json::Int64)row["id"].as<int64_t>();
                    catJson["id_user"] = (Json::Int64)row["id_user"].as<int64_t>();
                    catJson["name"] = row["name"].as<std::string>();
                    catJson["type"] = row["type"].as<std::string>();
                    catJson["is_family"] = true;
                    arr.append(catJson);
                }
            }
        } else {
            // Получаем только личные категории пользователя
            drogon::orm::CoroMapper<Category> mapper(db);
            std::vector<Category> cats = co_await mapper.findBy(
                drogon::orm::Criteria(Category::Cols::_id_user,
                                      drogon::orm::CompareOperator::EQ,
                                      static_cast<int32_t>(*userIdOpt))
                && drogon::orm::Criteria(Category::Cols::_is_family,
                                         drogon::orm::CompareOperator::EQ,
                                         false));
            for (const auto &c : cats) {
                auto catJson = c.toJson();
                // Проверяем, является ли категория семейной
                auto isFamilyPtr = c.getIsFamily();
                if (isFamilyPtr && *isFamilyPtr) {
                    catJson["is_family"] = true;
                } else {
                    catJson["is_family"] = false;
                }
                arr.append(catJson);
            }
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(arr);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetCategories error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> CategoryController::UpdateCategory(HttpRequestPtr req, int categoryId) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        bool isFamilyRequest = req->getParameter("family") == "true";

        auto json = req->getJsonObject();
        if (!json) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid JSON");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Category> mapper(db);

        auto cat = co_await mapper.findByPrimaryKey(categoryId);

        bool catIsFamily = cat.getIsFamily() && *cat.getIsFamily();
        if (catIsFamily != isFamilyRequest) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Category scope mismatch");
            co_return resp;
        }

        if (catIsFamily) {
            auto db = drogon::app().getFastDbClient();
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*category_update_family_scope*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(cat.getValueOfIdUser())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Category is not available for this family");
                co_return resp;
            }
        } else {
            if (cat.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Category does not belong to user");
                co_return resp;
            }
        }

        if (json->isMember("name")) {
            cat.setName((*json)["name"].asString());
        }
        if (json->isMember("type")) {
            std::string type = (*json)["type"].asString();
            if (type != "income" && type != "expense") {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Invalid type. Must be 'income' or 'expense'");
                co_return resp;
            }
            cat.setType(type);
        }

        co_await mapper.update(cat);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(cat.toJson());
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Category not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "UpdateCategory error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> CategoryController::DeleteCategory(HttpRequestPtr req, int categoryId) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        bool isFamilyRequest = req->getParameter("family") == "true";

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Category> mapper(db);
        auto cat = co_await mapper.findByPrimaryKey(categoryId);

        bool catIsFamily = cat.getIsFamily() && *cat.getIsFamily();
        if (catIsFamily != isFamilyRequest) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Category scope mismatch");
            co_return resp;
        }

        if (catIsFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*category_delete_family_scope*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(cat.getValueOfIdUser())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Category is not available for this family");
                co_return resp;
            }
        } else {
            if (cat.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Category does not belong to user");
                co_return resp;
            }
        }

        co_await mapper.deleteByPrimaryKey(categoryId);

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Category not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "DeleteCategory error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}
