#include "BudgetController.h"
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

Task<HttpResponsePtr> BudgetController::CreateBudget(HttpRequestPtr req) {
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

        if (!json->isMember("id_category") ||
            !json->isMember("month") ||
            !json->isMember("year") ||
            !json->isMember("limit_amount")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required fields: id_category, month, year, limit_amount");
            co_return resp;
        }

        // Семейный режим задаётся параметром family=true
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

        Budgets b;
        b.setIdUser(static_cast<int32_t>(*userIdOpt));
        b.setIdCategory((*json)["id_category"].asInt());
        b.setMonth((*json)["month"].asInt());
        b.setYear((*json)["year"].asInt());
        b.setLimitAmount((*json)["limit_amount"].asString());
        if (isFamily) {
            b.setIsFamily(true);
        } else {
            b.setIsFamily(false);
        }

        auto db = drogon::app().getFastDbClient();
        // Проверка на дубликат бюджета для той же категории/месяца/года в рамках режима (личный/семейный)
        if (isFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                "SELECT id_family FROM family_members WHERE id_user = $1", *userIdOpt
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("User is not a member of any family");
                co_return resp;
            }
            int64_t familyId = familyCheck[0]["id_family"].as<int64_t>();
            auto dup = co_await db->execSqlCoro(
                R"(
                /*budget_dup_family_v2*/
                SELECT 1 FROM budgets b
                JOIN family_members fm ON fm.id_user = b.id_user
                WHERE fm.id_family = $1::int8
                  AND b.id_category = $2::int4
                  AND b.month = $3::int4
                  AND b.year = $4::int4
                  AND b.is_family = TRUE
                LIMIT 1
                )",
                familyId,
                static_cast<int32_t>(b.getValueOfIdCategory()),
                static_cast<int32_t>(b.getValueOfMonth()),
                static_cast<int32_t>(b.getValueOfYear())
            );
            if (!dup.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Budget for this category and period already exists");
                co_return resp;
            }
        } else {
            auto dup = co_await db->execSqlCoro(
                R"(
                /*budget_dup_personal_v2*/
                SELECT 1 FROM budgets
                WHERE id_user = $1::int8
                  AND id_category = $2::int4
                  AND month = $3::int4
                  AND year = $4::int4
                  AND is_family = FALSE
                LIMIT 1
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int32_t>(b.getValueOfIdCategory()),
                static_cast<int32_t>(b.getValueOfMonth()),
                static_cast<int32_t>(b.getValueOfYear())
            );
            if (!dup.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Budget for this category and period already exists");
                co_return resp;
            }
        }

        drogon::orm::CoroMapper<Budgets> mapper(db);
        auto inserted = co_await mapper.insert(b);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(inserted.toJson());
        resp->setStatusCode(drogon::k201Created);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "CreateBudget error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> BudgetController::GetBudgets(HttpRequestPtr req) {
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
            auto familyBudgets = co_await db->execSqlCoro(
                R"(
                /*family_budgets_v3_ordered*/
                SELECT b.id, b.id_user, b.id_category, b.month, b.year, b.limit_amount, b.is_family, b.created_at
                FROM budgets b
                JOIN family_members fm ON fm.id_user = b.id_user
                WHERE fm.id_family IN (
                    SELECT fm2.id_family FROM family_members fm2 WHERE fm2.id_user = $1
                )
                AND b.is_family = TRUE
                ORDER BY b.year DESC, b.month DESC
                )", *userIdOpt
            );
            for (const auto &row : familyBudgets) {
                auto budgetJson = Budgets(row).toJson();
                budgetJson["is_family"] = true;
                arr.append(budgetJson);
            }
        } else {
            // Получаем только личные бюджеты, сортируем по дате
            auto personalBudgets = co_await db->execSqlCoro(
                R"(
                /*personal_budgets_v2_ordered*/
                SELECT id, id_user, id_category, month, year, limit_amount, is_family, created_at
                FROM budgets
                WHERE id_user = $1::int8
                  AND is_family = FALSE
                ORDER BY year DESC, month DESC
                )",
                static_cast<int64_t>(*userIdOpt)
            );
            for (const auto &row : personalBudgets) {
                auto b = Budgets(row);
                auto budgetJson = b.toJson();
                budgetJson["is_family"] = false;
                arr.append(budgetJson);
            }
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(arr);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetBudgets error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> BudgetController::UpdateBudget(HttpRequestPtr req, int budgetId) {
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
        drogon::orm::CoroMapper<Budgets> mapper(db);
        auto b = co_await mapper.findByPrimaryKey(budgetId);

        bool budgetIsFamily = b.getIsFamily() && *b.getIsFamily();
        if (budgetIsFamily != isFamilyRequest) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Budget scope mismatch");
            co_return resp;
        }

        if (budgetIsFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*budget_update_family_scope*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(b.getValueOfIdUser())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Budget is not available for this family");
                co_return resp;
            }
        } else {
            if (b.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Budget does not belong to user");
                co_return resp;
            }
        }

        int32_t newCategoryId = b.getValueOfIdCategory();
        int32_t newMonth = b.getValueOfMonth();
        int32_t newYear = b.getValueOfYear();
        std::string newLimit = b.getValueOfLimitAmount();

        if (json->isMember("id_category")) {
            newCategoryId = (*json)["id_category"].asInt();
        }
        if (json->isMember("month")) {
            newMonth = (*json)["month"].asInt();
        }
        if (json->isMember("year")) {
            newYear = (*json)["year"].asInt();
        }
        if (json->isMember("limit_amount")) {
            newLimit = (*json)["limit_amount"].asString();
        }

        // Проверяем категорию
        auto catRows = co_await db->execSqlCoro(
            "SELECT id_user, is_family FROM category WHERE id = $1", newCategoryId
        );
        if (catRows.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Category not found");
            co_return resp;
        }
        bool catIsFamily = !catRows[0]["is_family"].isNull() && catRows[0]["is_family"].as<bool>();
        if (catIsFamily != budgetIsFamily) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Category scope mismatch");
            co_return resp;
        }
        if (budgetIsFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*budget_update_cat_family*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(catRows[0]["id_user"].as<int64_t>())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Category is not available for this family");
                co_return resp;
            }
        } else {
            if (catRows[0]["id_user"].as<int>() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Category does not belong to user");
                co_return resp;
            }
        }

        // Проверяем дубликаты
        if (budgetIsFamily) {
            auto dupCheck = co_await db->execSqlCoro(
                R"(
                /*budget_dup_update_family_v1*/
                SELECT 1 FROM budgets b
                JOIN family_members fm1 ON fm1.id_user = b.id_user
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm2.id_user = $1::int8
                  AND b.id_category = $2::int4
                  AND b.month = $3::int4
                  AND b.year = $4::int4
                  AND b.is_family = TRUE
                  AND b.id <> $5::int4
                )",
                static_cast<int64_t>(*userIdOpt),
                newCategoryId,
                newMonth,
                newYear,
                budgetId
            );
            if (!dupCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Budget already exists for this category, month, and year");
                co_return resp;
            }
        } else {
            auto dupCheck = co_await db->execSqlCoro(
                R"(
                /*budget_dup_update_personal_v1*/
                SELECT 1 FROM budgets
                WHERE id_user = $1::int8
                  AND id_category = $2::int4
                  AND month = $3::int4
                  AND year = $4::int4
                  AND is_family = FALSE
                  AND id <> $5::int4
                )",
                static_cast<int64_t>(*userIdOpt),
                newCategoryId,
                newMonth,
                newYear,
                budgetId
            );
            if (!dupCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Budget already exists for this category, month, and year");
                co_return resp;
            }
        }

        // Применяем новые значения
        b.setIdUser(static_cast<int32_t>(*userIdOpt));
        b.setIdCategory(newCategoryId);
        b.setMonth(newMonth);
        b.setYear(newYear);
        b.setLimitAmount(newLimit);
        if (budgetIsFamily) {
            b.setIsFamily(true);
        } else {
            b.setIsFamily(false);
        }

        co_await mapper.update(b);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(b.toJson());
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Budget not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "UpdateBudget error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> BudgetController::DeleteBudget(HttpRequestPtr req, int budgetId) {
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
        drogon::orm::CoroMapper<Budgets> mapper(db);
        auto b = co_await mapper.findByPrimaryKey(budgetId);

        bool budgetIsFamily = b.getIsFamily() && *b.getIsFamily();
        if (budgetIsFamily != isFamilyRequest) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Budget scope mismatch");
            co_return resp;
        }

        if (budgetIsFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*budget_delete_family_scope*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(b.getValueOfIdUser())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Budget is not available for this family");
                co_return resp;
            }
        } else {
            if (b.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Budget does not belong to user");
                co_return resp;
            }
        }

        co_await mapper.deleteByPrimaryKey(budgetId);

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Budget not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "DeleteBudget error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

