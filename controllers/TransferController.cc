#include "TransferController.h"
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

static double parseAmount(const std::string &s) {
    try {
        return std::stod(s);
    } catch (...) {
        return 0.0;
    }
}

static std::string amountToString(double v) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << v;
    return oss.str();
}

Task<HttpResponsePtr> TransferController::CreateTransfer(HttpRequestPtr req) {
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

        if (!json->isMember("account_from") ||
            !json->isMember("account_to") ||
            !json->isMember("amount")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required fields: account_from, account_to, amount");
            co_return resp;
        }
        int32_t fromId = (*json)["account_from"].asInt();
        int32_t toId = (*json)["account_to"].asInt();
        std::string amountStr = (*json)["amount"].asString();
        double amount = parseAmount(amountStr);
        if (amount <= 0) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Amount must be positive");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Account> accMapper(db);
        drogon::orm::CoroMapper<Transfer> trMapper(db);

        auto fromAcc = co_await accMapper.findByPrimaryKey(fromId);
        auto toAcc = co_await accMapper.findByPrimaryKey(toId);

        if (fromAcc.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt) ||
            toAcc.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Accounts do not belong to user");
            co_return resp;
        }

        double fromBal = parseAmount(fromAcc.getValueOfBalance());
        double toBal = parseAmount(toAcc.getValueOfBalance());

        if (fromBal < amount) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Insufficient funds");
            co_return resp;
        }

        fromBal -= amount;
        toBal += amount;
        fromAcc.setBalance(amountToString(fromBal));
        toAcc.setBalance(amountToString(toBal));

        // Обновляем счета и записываем перевод
        co_await accMapper.update(fromAcc);
        co_await accMapper.update(toAcc);

        Transfer tr;
        tr.setIdUser(static_cast<int32_t>(*userIdOpt));
        tr.setAccountFrom(fromId);
        tr.setAccountTo(toId);
        tr.setAmount(amountToString(amount));

        auto inserted = co_await trMapper.insert(tr);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(inserted.toJson());
        resp->setStatusCode(drogon::k201Created);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Account not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "CreateTransfer error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> TransferController::GetTransfers(HttpRequestPtr req) {
    try {
        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Transfer> mapper(db);

        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        std::vector<Transfer> trs = co_await mapper.findBy(
            drogon::orm::Criteria(Transfer::Cols::_id_user,
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
        LOG_ERROR << "GetTransfers error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}


