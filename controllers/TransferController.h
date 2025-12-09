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
        ADD_METHOD_TO(TransferController::UpdateTransfer, "/transfers/{1}", drogon::Put);
        ADD_METHOD_TO(TransferController::DeleteTransfer, "/transfers/{1}", drogon::Delete);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> CreateTransfer(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> GetTransfers(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> UpdateTransfer(drogon::HttpRequestPtr req, int transferId);
    drogon::Task<drogon::HttpResponsePtr> DeleteTransfer(drogon::HttpRequestPtr req, int transferId);
};

}


