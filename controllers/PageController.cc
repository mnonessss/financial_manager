#include "PageController.h"
#include <drogon/HttpResponse.h>
#include <drogon/HttpViewData.h>

using namespace finance;

void PageController::Index(const drogon::HttpRequestPtr&,
                           std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    drogon::HttpViewData data;
    auto resp = drogon::HttpResponse::newHttpViewResponse("welcome.csp", data);
    callback(resp);
}

void PageController::HomePage(const drogon::HttpRequestPtr&,
                              std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    drogon::HttpViewData data;
    auto resp = drogon::HttpResponse::newHttpViewResponse("home.csp", data);
    callback(resp);
}

void PageController::CategoriesPage(const drogon::HttpRequestPtr&,
                                    std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    drogon::HttpViewData data;
    auto resp = drogon::HttpResponse::newHttpViewResponse("categories.csp", data);
    callback(resp);
}

void PageController::TransactionsPage(const drogon::HttpRequestPtr&,
                                      std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    drogon::HttpViewData data;
    auto resp = drogon::HttpResponse::newHttpViewResponse("transactions.csp", data);
    callback(resp);
}

void PageController::TransfersPage(const drogon::HttpRequestPtr&,
                                   std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    drogon::HttpViewData data;
    auto resp = drogon::HttpResponse::newHttpViewResponse("transfers.csp", data);
    callback(resp);
}

void PageController::BudgetsPage(const drogon::HttpRequestPtr&,
                                 std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    drogon::HttpViewData data;
    auto resp = drogon::HttpResponse::newHttpViewResponse("budgets.csp", data);
    callback(resp);
}


