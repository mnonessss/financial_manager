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

        // Получаем счет и проверяем права доступа
        Account account;
        try {
            account = co_await accMapper.findByPrimaryKey(idAccount);
            if (account.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Account does not belong to user");
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
        drogon::orm::CoroMapper<Transactions> mapper(db);

        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        std::vector<Transactions> trs = co_await mapper.findBy(
            drogon::orm::Criteria(Transactions::Cols::_id_user,
                                  drogon::orm::CompareOperator::EQ,
                                  static_cast<int32_t>(*userIdOpt)));

        Json::Value arr(Json::arrayValue);
        for (const auto &t : trs) {
            arr.append(t.toJson());
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

Task<HttpResponsePtr> TransactionsController::DeleteTransaction(
    HttpRequestPtr /*req*/, int transactionId) {
    try {
        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Transactions> mapper(db);
        co_await mapper.deleteByPrimaryKey(transactionId);

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


