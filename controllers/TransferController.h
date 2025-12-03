#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpBinder.h>
#include <drogon/orm/CoroMapper.h>
#include "models/Transfer.h"
#include "models/Account.h"

namespace finance {

class TransferController : public drogon::HttpController<TransferController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(TransferController::CreateTransfer, "/transfers", drogon::Post);
        ADD_METHOD_TO(TransferController::GetTransfers, "/transfers", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> CreateTransfer(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> GetTransfers(drogon::HttpRequestPtr req);
};

}


