#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpBinder.h>
#include <drogon/orm/CoroMapper.h>
#include "models/Account.h"

namespace finance {

class AccountController : public drogon::HttpController<AccountController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(AccountController::createAccount, "/accounts", drogon::Post);
        ADD_METHOD_TO(AccountController::GetAccounts, "/accounts", drogon::Get);
        ADD_METHOD_TO(AccountController::GetAccountById, "/accounts/{accountId}", drogon::Get);
        ADD_METHOD_TO(AccountController::UpdateAccount, "/accounts/{accountId}", drogon::Put);
        ADD_METHOD_TO(AccountController::DeleteAccount, "/accounts/{accountId}", drogon::Delete);
        ADD_METHOD_TO(AccountController::showCreateAccountForm, "/accounts/create", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> createAccount(
        drogon::HttpRequestPtr req);

    drogon::Task<drogon::HttpResponsePtr> GetAccounts(
        drogon::HttpRequestPtr req);

    drogon::Task<drogon::HttpResponsePtr> GetAccountById(
        drogon::HttpRequestPtr req, int accountId);

    drogon::Task<drogon::HttpResponsePtr> UpdateAccount(
        drogon::HttpRequestPtr req, int accountId);

    drogon::Task<drogon::HttpResponsePtr> DeleteAccount(
        drogon::HttpRequestPtr req, int accountId);

    void showCreateAccountForm(const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)> &&callback);

};

}
