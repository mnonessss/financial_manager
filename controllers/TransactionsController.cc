#include "TransactionsController.h"
#include <drogon/HttpResponse.h>
#include <drogon/orm/CoroMapper.h>
#include <jsoncpp/json/json.h>
#include <drogon/HttpAppFramework.h>
#include "utils/JwtUtils.h"
#include "models/Account.h"
#include <sstream>
#include <iomanip>

using namespace finance;
using namespace drogon_model::financial_manager;
using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
using drogon::Task;

Task<HttpResponsePtr> TransactionsController::createTransaction(HttpRequestPtr req) {
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

        if (!json->isMember("id_account") ||
            !json->isMember("amount") ||
            !json->isMember("type")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required fields: id_account, amount, type");
            co_return resp;
        }

        int32_t idAccount = (*json)["id_account"].asInt();
        std::string amount = (*json)["amount"].asString();
        std::string type = (*json)["type"].asString(); // income/expense
        // нормализуем тип транзакции
        std::transform(type.begin(), type.end(), type.begin(), ::tolower);
        std::string description;
        if (json->isMember("description")) {
            description = (*json)["description"].asString();
        }
        
        int32_t idCategory = 0;
        if (json->isMember("id_category")) {
            idCategory = (*json)["id_category"].asInt();
        }

        if (type != "income" && type != "expense") {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid type. Must be 'income' or 'expense'");
            co_return resp;
        }

        // Парсим сумму для обновления баланса
        double amountValue = 0.0;
        try {
            amountValue = std::stod(amount);
        } catch (...) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid amount format");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Transactions> trMapper(db);
        drogon::orm::CoroMapper<Account> accMapper(db);

        // Семейный режим задаётся параметром family=true
        bool isFamily = req->getParameter("family") == "true";
        if (isFamily) {
            // Проверяем, что пользователь состоит в семье
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

        // Если указана категория — проверяем тип и доступность
        if (idCategory > 0) {
            // Получаем категорию
            auto catRows = co_await db->execSqlCoro(
                "SELECT type, is_family FROM category WHERE id = $1", idCategory
            );
            if (catRows.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Category not found");
                co_return resp;
            }

            std::string catType = catRows[0]["type"].as<std::string>();
            std::transform(catType.begin(), catType.end(), catType.begin(), ::tolower);
            const bool catIsFamily = !catRows[0]["is_family"].isNull() && catRows[0]["is_family"].as<bool>();

            LOG_INFO << "[Tx] user=" << *userIdOpt << " isFamily=" << isFamily
                     << " account=" << idAccount << " category=" << idCategory
                     << " catType=" << catType << " reqType=" << type
                     << " catFamily=" << catIsFamily;

            // Проверяем совпадение типа категории и типа транзакции
            if (catType != type) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Category type does not match transaction type");
                co_return resp;
            }

            // Проверяем доступность категории: семейная категория только в семейном режиме и наоборот
            if (isFamily != catIsFamily) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Category is not available for this transaction scope");
                co_return resp;
            }
        }

        // Получаем счет и проверяем права доступа
        Account account;
        try {
            account = co_await accMapper.findByPrimaryKey(idAccount);
            bool hasAccess = false;
            bool accountIsFamily = account.getIsFamily() && *account.getIsFamily();
            if (isFamily && accountIsFamily) {
                // Проверяем, что счет принадлежит семье пользователя
                auto familyCheck = co_await db->execSqlCoro(
                    R"(
                    /*tx_family_access_v2*/
                    SELECT 1 FROM family_members fm1
                    JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                    WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                    )", *userIdOpt, static_cast<int64_t>(account.getValueOfIdUser())
                );
                hasAccess = !familyCheck.empty();
            } else if (!isFamily && !accountIsFamily) {
                hasAccess = (account.getValueOfIdUser() == static_cast<int32_t>(*userIdOpt));
            }
            
            if (!hasAccess) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Account does not belong to user or family");
                co_return resp;
            }
        } catch (const drogon::orm::UnexpectedRows &) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k404NotFound);
            resp->setBody("Account not found");
            co_return resp;
        }

        // Обновляем баланс счета
        double currentBalance = 0.0;
        try {
            currentBalance = std::stod(account.getValueOfBalance());
        } catch (...) {
            currentBalance = 0.0;
        }

        if (type == "income") {
            currentBalance += amountValue;
        } else {
            currentBalance -= amountValue;
            if (currentBalance < 0) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Insufficient funds");
                co_return resp;
            }
        }

        // Форматируем баланс обратно в строку
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(2);
        oss << currentBalance;
        account.setBalance(oss.str());
        co_await accMapper.update(account);

        // Создаем транзакцию
        Transactions tr;
        tr.setIdUser(static_cast<int32_t>(*userIdOpt));
        tr.setIdAccount(idAccount);
        tr.setAmount(amount);
        tr.setType(type);
        if (idCategory > 0) {
            tr.setIdCategory(idCategory);
        } else {
            tr.setIdCategoryToNull();
        }
        if (!description.empty()) {
            tr.setDescription(description);
        } else {
            tr.setDescriptionToNull();
        }
        if (isFamily) {
            tr.setIsFamily(true);
        } else {
            tr.setIsFamily(false);
        }

        auto inserted = co_await trMapper.insert(tr);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(inserted.toJson());
        resp->setStatusCode(drogon::k201Created);
        co_return resp;
    } catch (const drogon::orm::DrogonDbException &e) {
        LOG_ERROR << "createTransaction database error: " << e.base().what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Database error: " + std::string(e.base().what()));
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "createTransaction error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error: " + std::string(e.what()));
        co_return resp;
    } catch (...) {
        LOG_ERROR << "createTransaction unknown error";
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Unknown error occurred");
        co_return resp;
    }
}

Task<HttpResponsePtr> TransactionsController::GetTransactions(HttpRequestPtr req) {
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
            auto familyTransactions = co_await db->execSqlCoro(
                R"(
                /*family_transactions_v2*/
                SELECT t.*
                FROM transactions t
                JOIN family_members fm ON fm.id_user = t.id_user
                WHERE fm.id_family IN (
                    SELECT fm2.id_family FROM family_members fm2 WHERE fm2.id_user = $1
                )
                AND t.is_family = TRUE
                ORDER BY t.created_at DESC
                )", *userIdOpt
            );
            for (const auto &row : familyTransactions) {
                Transactions t(row);
                auto trJson = t.toJson();
                trJson["is_family"] = true;
                arr.append(trJson);
            }
        } else {
            // Получаем только личные транзакции с сортировкой по дате
            auto personalTransactions = co_await db->execSqlCoro(
                R"(
                /*personal_transactions_v2*/
                SELECT * FROM transactions
                WHERE id_user = $1::int8
                  AND is_family = FALSE
                ORDER BY created_at DESC
                )",
                static_cast<int64_t>(*userIdOpt)
            );
            for (const auto &row : personalTransactions) {
                Transactions t(row);
                auto trJson = t.toJson();
                trJson["is_family"] = false;
                arr.append(trJson);
            }
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(arr);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetTransactions error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> TransactionsController::GetTransactionById(
    HttpRequestPtr /*req*/, int transactionId) {
    try {
        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Transactions> mapper(db);
        auto tr = co_await mapper.findByPrimaryKey(transactionId);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(tr.toJson());
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Transaction not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetTransactionById error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> TransactionsController::UpdateTransaction(
    HttpRequestPtr req, int transactionId) {
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

        if (!json->isMember("id_account") ||
            !json->isMember("amount") ||
            !json->isMember("type")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required fields: id_account, amount, type");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Transactions> trMapper(db);
        drogon::orm::CoroMapper<Account> accMapper(db);

        // Получаем текущую транзакцию
        auto existing = co_await trMapper.findByPrimaryKey(transactionId);
        const bool txIsFamily = existing.getIsFamily() && *existing.getIsFamily();
        if (txIsFamily != isFamilyRequest) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Transaction scope mismatch");
            co_return resp;
        }

        // Проверка принадлежности транзакции пользователю / семье
        if (txIsFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*tx_update_family_scope*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(existing.getValueOfIdUser())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Transaction is not available for this family");
                co_return resp;
            }
        } else {
            if (existing.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Transaction does not belong to user");
                co_return resp;
            }
        }

        // Сохраняем старые значения для отката баланса
        const int32_t oldAccountId = existing.getValueOfIdAccount();
        std::string oldType = existing.getValueOfType();
        std::transform(oldType.begin(), oldType.end(), oldType.begin(), ::tolower);
        double oldAmount = 0.0;
        try {
            oldAmount = std::stod(existing.getValueOfAmount());
        } catch (...) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid stored amount");
            co_return resp;
        }

        // Новые значения
        const int32_t newAccountId = (*json)["id_account"].asInt();
        std::string newAmountStr = (*json)["amount"].asString();
        std::string newType = (*json)["type"].asString();
        std::transform(newType.begin(), newType.end(), newType.begin(), ::tolower);
        if (newType != "income" && newType != "expense") {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid type. Must be 'income' or 'expense'");
            co_return resp;
        }
        double newAmount = 0.0;
        try {
            newAmount = std::stod(newAmountStr);
        } catch (...) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid amount format");
            co_return resp;
        }

        int32_t newCategoryId = 0;
        if (json->isMember("id_category")) {
            newCategoryId = (*json)["id_category"].asInt();
        }
        std::string newDescription;
        if (json->isMember("description")) {
            newDescription = (*json)["description"].asString();
        }

        // Проверяем категорию (если указана)
        if (newCategoryId > 0) {
            auto catRows = co_await db->execSqlCoro(
                "SELECT type, is_family FROM category WHERE id = $1", newCategoryId
            );
            if (catRows.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Category not found");
                co_return resp;
            }
            std::string catType = catRows[0]["type"].as<std::string>();
            std::transform(catType.begin(), catType.end(), catType.begin(), ::tolower);
            const bool catIsFamily = !catRows[0]["is_family"].isNull() && catRows[0]["is_family"].as<bool>();
            if (catType != newType) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Category type does not match transaction type");
                co_return resp;
            }
            if (catIsFamily != txIsFamily) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Category is not available for this transaction scope");
                co_return resp;
            }
        }

        // Откат баланса старого счета
        auto oldAccount = co_await accMapper.findByPrimaryKey(oldAccountId);
        bool oldAccFamily = oldAccount.getIsFamily() && *oldAccount.getIsFamily();
        bool hasAccessOldAcc = false;
        if (txIsFamily && oldAccFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*tx_update_old_acc_family*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(oldAccount.getValueOfIdUser())
            );
            hasAccessOldAcc = !familyCheck.empty();
        } else if (!txIsFamily && !oldAccFamily) {
            hasAccessOldAcc = (oldAccount.getValueOfIdUser() == static_cast<int32_t>(*userIdOpt));
        }
        if (!hasAccessOldAcc) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Account does not belong to user or family");
            co_return resp;
        }

        auto revertBalance = [&](double balance) {
            if (oldType == "income") {
                return balance - oldAmount;
            } else {
                return balance + oldAmount;
            }
        };

        double oldAccBalance = 0.0;
        try {
            oldAccBalance = std::stod(oldAccount.getValueOfBalance());
        } catch (...) {
            oldAccBalance = 0.0;
        }
        oldAccBalance = revertBalance(oldAccBalance);
        {
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(2);
            oss << oldAccBalance;
            oldAccount.setBalance(oss.str());
        }

        // Проверяем новый счет
        auto newAccount = co_await accMapper.findByPrimaryKey(newAccountId);
        bool newAccFamily = newAccount.getIsFamily() && *newAccount.getIsFamily();
        bool hasAccessNewAcc = false;
        if (txIsFamily && newAccFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*tx_update_new_acc_family*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(newAccount.getValueOfIdUser())
            );
            hasAccessNewAcc = !familyCheck.empty();
        } else if (!txIsFamily && !newAccFamily) {
            hasAccessNewAcc = (newAccount.getValueOfIdUser() == static_cast<int32_t>(*userIdOpt));
        }

        if (!hasAccessNewAcc) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Account does not belong to user or family");
            co_return resp;
        }

        if (newAccFamily != txIsFamily) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Account scope mismatch");
            co_return resp;
        }

        // Применяем новое изменение баланса
        double newAccBalance = 0.0;
        try {
            newAccBalance = std::stod(newAccount.getValueOfBalance());
        } catch (...) {
            newAccBalance = 0.0;
        }
        if (oldAccountId == newAccountId) {
            // если один и тот же счет, берём уже откатанный баланс
            newAccBalance = oldAccBalance;
        }

        if (newType == "income") {
            newAccBalance += newAmount;
        } else {
            newAccBalance -= newAmount;
            if (newAccBalance < 0) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Insufficient funds");
                co_return resp;
            }
        }

        {
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(2);
            oss << newAccBalance;
            newAccount.setBalance(oss.str());
        }

        // Сохраняем балансы
        if (oldAccountId == newAccountId) {
            co_await accMapper.update(newAccount);
        } else {
            co_await accMapper.update(oldAccount);
            co_await accMapper.update(newAccount);
        }

        // Обновляем транзакцию
        existing.setIdUser(static_cast<int32_t>(*userIdOpt));
        existing.setIdAccount(newAccountId);
        existing.setAmount(newAmountStr);
        existing.setType(newType);
        if (newCategoryId > 0) {
            existing.setIdCategory(newCategoryId);
        } else {
            existing.setIdCategoryToNull();
        }
        if (!newDescription.empty()) {
            existing.setDescription(newDescription);
        } else {
            existing.setDescriptionToNull();
        }
        if (txIsFamily) {
            existing.setIsFamily(true);
        } else {
            existing.setIsFamily(false);
        }

        co_await trMapper.update(existing);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(existing.toJson());
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Transaction not found");
        co_return resp;
    } catch (const drogon::orm::DrogonDbException &e) {
        LOG_ERROR << "UpdateTransaction database error: " << e.base().what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Database error: " + std::string(e.base().what()));
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "UpdateTransaction error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> TransactionsController::DeleteTransaction(
    HttpRequestPtr req, int transactionId) {
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
        drogon::orm::CoroMapper<Transactions> trMapper(db);
        drogon::orm::CoroMapper<Account> accMapper(db);

        auto tr = co_await trMapper.findByPrimaryKey(transactionId);
        const bool txIsFamily = tr.getIsFamily() && *tr.getIsFamily();
        if (txIsFamily != isFamilyRequest) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Transaction scope mismatch");
            co_return resp;
        }

        if (txIsFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*tx_delete_family_scope*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(tr.getValueOfIdUser())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Transaction is not available for this family");
                co_return resp;
            }
        } else {
            if (tr.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Transaction does not belong to user");
                co_return resp;
            }
        }

        // Получаем счет и проверяем доступ
        auto account = co_await accMapper.findByPrimaryKey(tr.getValueOfIdAccount());
        bool accIsFamily = account.getIsFamily() && *account.getIsFamily();
        bool hasAccessAcc = false;
        if (txIsFamily && accIsFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*tx_delete_acc_family*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(account.getValueOfIdUser())
            );
            hasAccessAcc = !familyCheck.empty();
        } else if (!txIsFamily && !accIsFamily) {
            hasAccessAcc = (account.getValueOfIdUser() == static_cast<int32_t>(*userIdOpt));
        }
        if (!hasAccessAcc) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Account does not belong to user or family");
            co_return resp;
        }

        // Откатываем баланс
        std::string type = tr.getValueOfType();
        std::transform(type.begin(), type.end(), type.begin(), ::tolower);
        double amount = 0.0;
        try {
            amount = std::stod(tr.getValueOfAmount());
        } catch (...) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid amount format");
            co_return resp;
        }

        double balance = 0.0;
        try {
            balance = std::stod(account.getValueOfBalance());
        } catch (...) {
            balance = 0.0;
        }

        if (type == "income") {
            balance -= amount;
        } else {
            balance += amount;
        }

        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(2);
        oss << balance;
        account.setBalance(oss.str());
        co_await accMapper.update(account);

        co_await trMapper.deleteByPrimaryKey(transactionId);

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Transaction not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "DeleteTransaction error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

