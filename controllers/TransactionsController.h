#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpBinder.h>
#include <drogon/orm/CoroMapper.h>
#include "models/Transactions.h"

namespace finance {

class TransactionsController : public drogon::HttpController<TransactionsController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(TransactionsController::createTransaction, "/transactions", drogon::Post);
        ADD_METHOD_TO(TransactionsController::GetTransactions, "/transactions", drogon::Get);
        ADD_METHOD_TO(TransactionsController::GetTransactionById, "/transactions/{1}", drogon::Get);
        ADD_METHOD_TO(TransactionsController::DeleteTransaction, "/transactions/{1}", drogon::Delete);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> createTransaction(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> GetTransactions(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> GetTransactionById(drogon::HttpRequestPtr req, int transactionId);
    drogon::Task<drogon::HttpResponsePtr> DeleteTransaction(drogon::HttpRequestPtr req, int transactionId);
};

}


