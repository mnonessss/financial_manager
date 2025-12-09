#pragma once

#include <drogon/HttpController.h>

namespace finance {

class PageController : public drogon::HttpController<PageController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(PageController::Index, "/", drogon::Get);
        ADD_METHOD_TO(PageController::HomePage, "/home", drogon::Get);
        ADD_METHOD_TO(PageController::CategoriesPage, "/ui/categories", drogon::Get);
        ADD_METHOD_TO(PageController::TransactionsPage, "/ui/transactions", drogon::Get);
        ADD_METHOD_TO(PageController::TransfersPage, "/ui/transfers", drogon::Get);
        ADD_METHOD_TO(PageController::BudgetsPage, "/ui/budgets", drogon::Get);
    METHOD_LIST_END

    void Index(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)> &&callback);
    drogon::Task<drogon::HttpResponsePtr> HomePage(drogon::HttpRequestPtr req);
    void CategoriesPage(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)> &&callback);
    void TransactionsPage(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)> &&callback);
    void TransfersPage(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)> &&callback);
    void BudgetsPage(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)> &&callback);
};

}


