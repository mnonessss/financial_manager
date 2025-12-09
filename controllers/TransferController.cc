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

        auto fromAcc = co_await accMapper.findByPrimaryKey(fromId);
        auto toAcc = co_await accMapper.findByPrimaryKey(toId);

        // Проверяем права доступа к счетам
        bool hasAccessFrom = false;
        bool hasAccessTo = false;
        
        bool fromAccIsFamily = fromAcc.getIsFamily() && *fromAcc.getIsFamily();
        bool toAccIsFamily = toAcc.getIsFamily() && *toAcc.getIsFamily();
        
        if (isFamily && fromAccIsFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*transfer_family_from_v2*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )", static_cast<int64_t>(*userIdOpt), static_cast<int64_t>(fromAcc.getValueOfIdUser())
            );
            hasAccessFrom = !familyCheck.empty();
        } else if (!isFamily && !fromAccIsFamily) {
            hasAccessFrom = (fromAcc.getValueOfIdUser() == static_cast<int32_t>(*userIdOpt));
        }
        
        if (isFamily && toAccIsFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*transfer_family_to_v2*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )", static_cast<int64_t>(*userIdOpt), static_cast<int64_t>(toAcc.getValueOfIdUser())
            );
            hasAccessTo = !familyCheck.empty();
        } else if (!isFamily && !toAccIsFamily) {
            hasAccessTo = (toAcc.getValueOfIdUser() == static_cast<int32_t>(*userIdOpt));
        }

        if (!hasAccessFrom || !hasAccessTo) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Accounts do not belong to user or family");
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
        if (isFamily) {
            tr.setIsFamily(true);
        } else {
            tr.setIsFamily(false);
        }

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
            // Получаем только семейные переводы всех членов семьи
            auto familyTransfers = co_await db->execSqlCoro(
                R"(
                /*family_transfers_v2*/
                SELECT t.*
                FROM transfer t
                JOIN family_members fm ON fm.id_user = t.id_user
                WHERE fm.id_family IN (
                    SELECT fm2.id_family FROM family_members fm2 WHERE fm2.id_user = $1
                )
                AND t.is_family = TRUE
                ORDER BY t.created_at DESC
                )", *userIdOpt
            );
            for (const auto &row : familyTransfers) {
                Transfer t(row);
                auto trJson = t.toJson();
                trJson["is_family"] = true;
                arr.append(trJson);
            }
        } else {
            // Получаем только личные переводы
            auto personalTransfers = co_await db->execSqlCoro(
                R"(
                /*personal_transfers_v2*/
                SELECT * FROM transfer
                WHERE id_user = $1::int8
                  AND is_family = FALSE
                ORDER BY created_at DESC
                )", static_cast<int64_t>(*userIdOpt)
            );
            for (const auto &row : personalTransfers) {
                Transfer t(row);
                auto trJson = t.toJson();
                trJson["is_family"] = false;
                arr.append(trJson);
            }
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

Task<HttpResponsePtr> TransferController::UpdateTransfer(HttpRequestPtr req, int transferId) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        bool isFamily = req->getParameter("family") == "true";

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

        int32_t newFromId = (*json)["account_from"].asInt();
        int32_t newToId = (*json)["account_to"].asInt();
        if (newFromId == newToId) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("account_from and account_to must be different");
            co_return resp;
        }
        std::string newAmountStr = (*json)["amount"].asString();
        double newAmount = parseAmount(newAmountStr);
        if (newAmount <= 0) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Amount must be positive");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Account> accMapper(db);
        drogon::orm::CoroMapper<Transfer> trMapper(db);

        auto existing = co_await trMapper.findByPrimaryKey(transferId);
        const bool trFamily = existing.getIsFamily() && *existing.getIsFamily();
        if (trFamily != isFamily) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Transfer scope mismatch");
            co_return resp;
        }

        if (trFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*transfer_update_scope*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )", static_cast<int64_t>(*userIdOpt), static_cast<int64_t>(existing.getValueOfIdUser())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Transfer is not available for this family");
                co_return resp;
            }
        } else {
            if (existing.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Transfer does not belong to user");
                co_return resp;
            }
        }

        // Счета из старого перевода
        int32_t oldFromId = existing.getValueOfAccountFrom();
        int32_t oldToId = existing.getValueOfAccountTo();
        double oldAmount = parseAmount(existing.getValueOfAmount());

        auto oldFromAcc = co_await accMapper.findByPrimaryKey(oldFromId);
        auto oldToAcc = co_await accMapper.findByPrimaryKey(oldToId);

        auto checkAccAccess = [&](const Account &acc) -> drogon::Task<bool> {
            bool accIsFamily = acc.getIsFamily() && *acc.getIsFamily();
            if (trFamily && accIsFamily) {
                auto familyCheck = co_await db->execSqlCoro(
                    R"(
                    /*transfer_update_acc_access*/
                    SELECT 1 FROM family_members fm1
                    JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                    WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                    )", static_cast<int64_t>(*userIdOpt), static_cast<int64_t>(acc.getValueOfIdUser())
                );
                co_return !familyCheck.empty();
            } else if (!trFamily && !accIsFamily) {
                co_return acc.getValueOfIdUser() == static_cast<int32_t>(*userIdOpt);
            }
            co_return false;
        };

        if (!(co_await checkAccAccess(oldFromAcc))) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Source account not accessible");
            co_return resp;
        }
        if (!(co_await checkAccAccess(oldToAcc))) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Target account not accessible");
            co_return resp;
        }

        // Откатываем старый перевод
        double oldFromBal = parseAmount(oldFromAcc.getValueOfBalance());
        double oldToBal = parseAmount(oldToAcc.getValueOfBalance());
        oldFromBal += oldAmount;
        oldToBal -= oldAmount;
        if (oldToBal < 0) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Cannot revert transfer: negative balance");
            co_return resp;
        }
        oldFromAcc.setBalance(amountToString(oldFromBal));
        oldToAcc.setBalance(amountToString(oldToBal));

        // Проверяем новые счета
        auto newFromAcc = co_await accMapper.findByPrimaryKey(newFromId);
        auto newToAcc = co_await accMapper.findByPrimaryKey(newToId);

        if (!(co_await checkAccAccess(newFromAcc))) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Source account not accessible");
            co_return resp;
        }
        if (!(co_await checkAccAccess(newToAcc))) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Target account not accessible");
            co_return resp;
        }

        bool fromFamily = newFromAcc.getIsFamily() && *newFromAcc.getIsFamily();
        bool toFamily = newToAcc.getIsFamily() && *newToAcc.getIsFamily();
        if (fromFamily != trFamily || toFamily != trFamily) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Accounts scope mismatch");
            co_return resp;
        }

        // Применяем новый перевод
        double newFromBal = (newFromId == oldFromId) ? oldFromBal : parseAmount(newFromAcc.getValueOfBalance());
        double newToBal = (newToId == oldToId) ? oldToBal : parseAmount(newToAcc.getValueOfBalance());

        newFromBal -= newAmount;
        if (newFromBal < 0) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Insufficient funds");
            co_return resp;
        }
        newToBal += newAmount;

        newFromAcc.setBalance(amountToString(newFromBal));
        newToAcc.setBalance(amountToString(newToBal));

        // Сохраняем счета
        if (newFromId == oldFromId) {
            co_await accMapper.update(newFromAcc);
        } else {
            co_await accMapper.update(oldFromAcc);
            co_await accMapper.update(newFromAcc);
        }

        if (newToId == oldToId) {
            if (newToId != newFromId) {
                co_await accMapper.update(newToAcc);
            }
        } else {
            co_await accMapper.update(oldToAcc);
            co_await accMapper.update(newToAcc);
        }

        // Обновляем перевод
        existing.setIdUser(static_cast<int32_t>(*userIdOpt));
        existing.setAccountFrom(newFromId);
        existing.setAccountTo(newToId);
        existing.setAmount(amountToString(newAmount));
        existing.setIsFamily(trFamily);

        co_await trMapper.update(existing);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(existing.toJson());
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Transfer not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "UpdateTransfer error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> TransferController::DeleteTransfer(HttpRequestPtr req, int transferId) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        bool isFamily = req->getParameter("family") == "true";

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Account> accMapper(db);
        drogon::orm::CoroMapper<Transfer> trMapper(db);

        auto tr = co_await trMapper.findByPrimaryKey(transferId);
        const bool trFamily = tr.getIsFamily() && *tr.getIsFamily();
        if (trFamily != isFamily) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Transfer scope mismatch");
            co_return resp;
        }

        if (trFamily) {
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*transfer_delete_scope*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )", static_cast<int64_t>(*userIdOpt), static_cast<int64_t>(tr.getValueOfIdUser())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Transfer is not available for this family");
                co_return resp;
            }
        } else {
            if (tr.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Transfer does not belong to user");
                co_return resp;
            }
        }

        int32_t fromId = tr.getValueOfAccountFrom();
        int32_t toId = tr.getValueOfAccountTo();
        double amount = parseAmount(tr.getValueOfAmount());

        auto fromAcc = co_await accMapper.findByPrimaryKey(fromId);
        auto toAcc = co_await accMapper.findByPrimaryKey(toId);

        auto checkAccAccess = [&](const Account &acc) -> drogon::Task<bool> {
            bool accIsFamily = acc.getIsFamily() && *acc.getIsFamily();
            if (trFamily && accIsFamily) {
                auto familyCheck = co_await db->execSqlCoro(
                    R"(
                    /*transfer_delete_acc*/
                    SELECT 1 FROM family_members fm1
                    JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                    WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                    )", static_cast<int64_t>(*userIdOpt), static_cast<int64_t>(acc.getValueOfIdUser())
                );
                co_return !familyCheck.empty();
            } else if (!trFamily && !accIsFamily) {
                co_return acc.getValueOfIdUser() == static_cast<int32_t>(*userIdOpt);
            }
            co_return false;
        };

        if (!(co_await checkAccAccess(fromAcc))) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Source account not accessible");
            co_return resp;
        }
        if (!(co_await checkAccAccess(toAcc))) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Target account not accessible");
            co_return resp;
        }

        double fromBal = parseAmount(fromAcc.getValueOfBalance());
        double toBal = parseAmount(toAcc.getValueOfBalance());

        fromBal += amount;
        toBal -= amount;
        if (toBal < 0) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Cannot revert transfer: negative balance");
            co_return resp;
        }

        fromAcc.setBalance(amountToString(fromBal));
        toAcc.setBalance(amountToString(toBal));

        co_await accMapper.update(fromAcc);
        co_await accMapper.update(toAcc);

        co_await trMapper.deleteByPrimaryKey(transferId);

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Transfer not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "DeleteTransfer error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

