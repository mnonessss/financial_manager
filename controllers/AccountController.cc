#include "AccountController.h"
#include <drogon/HttpResponse.h>
#include <drogon/orm/CoroMapper.h>
#include <jsoncpp/json/json.h>
#include <drogon/utils/Utilities.h>
#include <drogon/HttpViewData.h>
#include <optional>
#include <cstdlib>
#include "models/Account.h"
#include "utils/JwtUtils.h"


using namespace finance;
using namespace drogon_model::financial_manager;
using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
using drogon::Task;

Task<HttpResponsePtr> AccountController::createAccount(
    HttpRequestPtr req) {

    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }
        // 1. Проверка JSON
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid JSON");
            co_return resp;
        }

        // 2. Валидация полей
        if (!json->isMember("account_name") || 
            !json->isMember("account_type")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required fields: account_name, account_type");
            co_return resp;
        }

        std::string account_name = (*json)["account_name"].asString();
        std::string account_type = (*json)["account_type"].asString();
        std::string balance = "0";
        
        // Проверяем начальный баланс, если указан
        if (json->isMember("balance")) {
            balance = (*json)["balance"].asString();
            // Валидация баланса
            try {
                double balanceValue = std::stod(balance);
                if (balanceValue < 0) {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k400BadRequest);
                    resp->setBody("Balance cannot be negative");
                    co_return resp;
                }
            } catch (...) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Invalid balance format");
                co_return resp;
            }
        }

        // 3. Валидация типа счёта
        if (account_type != "cash" && account_type != "card" && account_type != "deposit") {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid account_type. Must be 'cash', 'card', or 'deposit'");
            co_return resp;
        }

        // 4. Семейный режим задаётся параметром family=true
        bool isFamily = req->getParameter("family") == "true";
        if (isFamily) {
            // Проверяем, что пользователь состоит в семье
            auto db = drogon::app().getFastDbClient();
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

        // 5. Создаём объект модели
        Account account;
        account.setIdUser(static_cast<int32_t>(*userIdOpt));
        account.setAccountType(account_type);
        account.setAccountName(account_name);
        account.setBalance(balance);
        if (isFamily) {
            account.setIsFamily(true);
        }
        else {
            account.setIsFamily(false);
        }

        // 6. Вставляем через ORM
        auto db = drogon::app().getFastDbClient();
        auto mapper = drogon::orm::CoroMapper<Account>(db);
        auto inserted = co_await mapper.insert(account);

        // 6. Формируем ответ
        Json::Value result;
        result["id"] = (Json::UInt64)inserted.getValueOfId();
        result["id_user"] = (Json::Int64)inserted.getValueOfIdUser();
        result["account_name"] = inserted.getValueOfAccountName();
        result["account_type"] = inserted.getValueOfAccountType();
        result["balance"] = inserted.getValueOfBalance();
        const trantor::Date &createdAt = inserted.getValueOfCreatedAt();
        std::time_t t = createdAt.secondsSinceEpoch();
        std::tm tm = *std::localtime(&t);  // localtime = местное время; gmtime = UTC

        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        result["created_at"] = std::string(buf);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k201Created);
        co_return resp;

    } catch (const std::exception &e) {
        LOG_ERROR << "Account creation error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> AccountController::GetAccounts(HttpRequestPtr req) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        auto mapper = drogon::orm::CoroMapper<Account>(db);
        bool familyView = req->getParameter("family") == "true";

        std::vector<Account> accounts;

        if (familyView) {
            // Проверяем членство и получаем семейные счета всех членов семьи
            auto familyCheck = co_await db->execSqlCoro(
                "SELECT id_family FROM family_members WHERE id_user = $1", *userIdOpt
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("User is not a member of any family");
                co_return resp;
            }
            int64_t familyId = familyCheck[0]["id_family"].as<int64_t>();
            auto familyAccounts = co_await db->execSqlCoro(
                R"(
                SELECT a.id, a.id_user, a.account_type, a.account_name, a.balance, a.created_at, a.is_family
                FROM account a
                JOIN family_members fm ON fm.id_user = a.id_user
                WHERE fm.id_family = $1 AND a.is_family = TRUE
                ORDER BY a.created_at DESC
                )",
                familyId
            );
            for (const auto &row : familyAccounts) {
                accounts.emplace_back(Account(row));
            }
        } else {
            // Личные счета текущего пользователя (без семейных), сортировка по дате
            auto personalAccounts = co_await db->execSqlCoro(
                R"(
                /*personal_accounts_v3_ordered*/
                SELECT id, id_user, account_type, account_name, balance, created_at, is_family
                FROM account
                WHERE id_user = $1::int8
                  AND is_family = FALSE
                ORDER BY created_at DESC
                )",
                static_cast<int64_t>(*userIdOpt)
            );
            for (const auto &row : personalAccounts) {
                accounts.emplace_back(Account(row));
            }
        }

        Json::Value arr(Json::arrayValue);
        for (const auto &acc : accounts) {
            auto accJson = acc.toJson();
            // Убеждаемся, что is_family правильно установлен
            auto isFamilyPtr = acc.getIsFamily();
            if (isFamilyPtr) {
                accJson["is_family"] = *isFamilyPtr;
            } else {
                accJson["is_family"] = false;
            }
            arr.append(accJson);
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(arr);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetAccounts error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> AccountController::GetAccountById(
    HttpRequestPtr /*req*/, int accountId) {
    try {
        auto db = drogon::app().getFastDbClient();
        auto mapper = drogon::orm::CoroMapper<Account>(db);
        auto account = co_await mapper.findByPrimaryKey(accountId);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(account.toJson());
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Account not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetAccountById error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> AccountController::UpdateAccount(
    HttpRequestPtr req, int accountId) {
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

        bool isFamilyRequest = req->getParameter("family") == "true";

        auto db = drogon::app().getFastDbClient();
        auto mapper = drogon::orm::CoroMapper<Account>(db);
        auto account = co_await mapper.findByPrimaryKey(accountId);

        bool accIsFamily = account.getIsFamily() && *account.getIsFamily();
        if (accIsFamily != isFamilyRequest) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Account scope mismatch");
            co_return resp;
        }

        if (accIsFamily) {
            // Проверяем, что пользователь и владелец счета в одной семье
            auto familyCheck = co_await db->execSqlCoro(
                R"(
                /*account_update_family_scope*/
                SELECT 1 FROM family_members fm1
                JOIN family_members fm2 ON fm1.id_family = fm2.id_family
                WHERE fm1.id_user = $1::int8 AND fm2.id_user = $2::int8
                )",
                static_cast<int64_t>(*userIdOpt),
                static_cast<int64_t>(account.getValueOfIdUser())
            );
            if (familyCheck.empty()) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Account does not belong to user or family");
                co_return resp;
            }
        } else {
            if (account.getValueOfIdUser() != static_cast<int32_t>(*userIdOpt)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k403Forbidden);
                resp->setBody("Account does not belong to user");
                co_return resp;
            }
        }

        if (json->isMember("account_name")) {
            account.setAccountName((*json)["account_name"].asString());
        }
        if (json->isMember("account_type")) {
            std::string account_type = (*json)["account_type"].asString();
            if (account_type != "cash" && account_type != "card" && account_type != "deposit") {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Invalid account_type. Must be 'cash', 'card', or 'deposit'");
                co_return resp;
            }
            account.setAccountType(account_type);
        }
        if (json->isMember("balance")) {
            std::string balance = (*json)["balance"].asString();
            try {
                double balanceValue = std::stod(balance);
                if (balanceValue < 0) {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k400BadRequest);
                    resp->setBody("Balance cannot be negative");
                    co_return resp;
                }
            } catch (...) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                resp->setBody("Invalid balance format");
                co_return resp;
            }
            account.setBalance(balance);
        }

        co_await mapper.update(account);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(account.toJson());
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Account not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "UpdateAccount error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> AccountController::DeleteAccount(
    HttpRequestPtr /*req*/, int accountId) {
    try {
        auto db = drogon::app().getFastDbClient();
        auto mapper = drogon::orm::CoroMapper<Account>(db);
        co_await mapper.deleteByPrimaryKey(accountId);

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("Account not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "DeleteAccount error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

void AccountController::showCreateAccountForm(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    bool isFamily = req->getParameter("family") == "true";
    std::string pageTitle = isFamily ? "Создать семейный счёт" : "Создать счёт";
    
    std::string html;
    html += "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n    <meta charset=\"UTF-8\" />\n    <title>" + pageTitle + " - Financial Manager</title>\n";
    html += "    <link rel=\"stylesheet\" href=\"https://unpkg.com/sakura.css/css/sakura.css\" />\n";
    html += R"HTML(
    <style>
        :root {
            --green: #2e7d32;
            --green-dark: #1b5e20;
            --surface: #f8fbf7;
            --border: #cde8cd;
            --muted: #5c6f5c;
        }
        body {
            max-width: 1100px;
            margin: 0 auto;
            padding: 24px;
            background: var(--surface);
            color: #1f2d1f;
        }
        h1, h2 { color: var(--green-dark); }
        a { color: var(--green-dark); }
        button, input[type="submit"], input[type="button"] {
            background: var(--green);
            color: #fff;
            border: none;
            border-radius: 8px;
            padding: 8px 12px;
            cursor: pointer;
            transition: all 0.15s ease;
        }
        button:hover, input[type="submit"]:hover, input[type="button"]:hover {
            background: var(--green-dark);
            transform: translateY(-1px);
        }
        form {
            background: #fff;
            border: 1px solid var(--border);
            border-radius: 10px;
            padding: 16px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.06);
            margin-bottom: 16px;
        }
        label {
            display: block;
            margin-bottom: 10px;
            color: var(--muted);
        }
        input[type=\"text\"], input[type=\"number\"], select {
            width: 100%;
            padding: 8px;
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-top: 4px;
            box-sizing: border-box;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            background: #fff;
            border: 1px solid var(--border);
            border-radius: 10px;
            overflow: hidden;
            box-shadow: 0 4px 12px rgba(0,0,0,0.06);
            margin-top: 1em;
        }
        th {
            background: #e8f3e7;
            color: var(--green-dark);
            padding: 0.6em;
            border-bottom: 2px solid var(--border);
        }
        td {
            padding: 0.6em;
            border-bottom: 1px solid var(--border);
        }
        tr:nth-child(even) td { background: #f5fbf4; }
        tr:last-child td { border-bottom: none; }
        .actions {
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }
        .actions button:nth-child(2) { background: #dc3545; }
        .actions button:nth-child(2):hover { background: #b42b38; }
    </style>
)HTML";
    html += "</head>\n<body>\n    <h1>" + pageTitle + "</h1>\n";
    html += R"HTML(    <form id="accountForm">
        <label>Название счёта
            <input type="text" name="account_name" required />
        </label>
        <label>Тип счёта
            <select name="account_type" required>
                <option value="cash">Наличные</option>
                <option value="card">Карта</option>
                <option value="deposit">Депозит</option>
            </select>
        </label>
        <label>Начальный баланс
            <input type="number" name="balance" step="0.01" min="0" value="0" placeholder="0.00" />
        </label>
)HTML";
    
    if (isFamily) {
        html += R"HTML(        <input type="hidden" name="is_family" value="true" />
)HTML";
    }
    
    html += R"HTML(        <button type="submit">Создать</button>
    </form>
    <p id="result"></p>

    <h2>Список счетов</h2>
    <div id="accountsContainer">
        <p id="loadingMessage">Загрузка...</p>
        <div id="emptyMessage" style="display: none;">
            <p style="color: #666; font-style: italic;">Пока не было добавлено ни одного счёта</p>
        </div>
        <table id="accountsTable" style="display: none;">
            <thead>
                <tr>
                    <th>Название</th>
                    <th>Тип</th>
                    <th>Баланс</th>
                    <th>Тип доступа</th>
                    <th>Дата создания</th>
                    <th>Действия</th>
                </tr>
            </thead>
            <tbody id="accountsTableBody">
            </tbody>
        </table>
    </div>
    <p><a href="/home">← Вернуться на главную</a></p>

    <!-- Модальное окно для редактирования -->
    <div id="editModal" style="display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.5);">
        <div style="background-color: #fefefe; margin: 15% auto; padding: 20px; border: 1px solid #888; width: 80%; max-width: 500px; border-radius: 5px;">
            <h2>Редактировать счёт</h2>
            <form id="editAccountForm">
                <input type="hidden" id="editAccountId" />
                <label>Название счёта
                    <input type="text" id="editAccountName" required />
                </label>
                <label>Тип счёта
                    <select id="editAccountType" required>
                        <option value="cash">Наличные</option>
                        <option value="card">Карта</option>
                        <option value="deposit">Депозит</option>
                    </select>
                </label>
                <label>Баланс
                    <input type="number" id="editAccountBalance" step="0.01" min="0" required />
                </label>
                <div style="margin-top: 15px;">
                    <button type="submit" style="background-color: #2196f3; color: white; border: none; padding: 8px 15px; border-radius: 3px; cursor: pointer; margin-right: 10px;">Сохранить</button>
                    <button type="button" onclick="closeEditModal()" style="background-color: #666; color: white; border: none; padding: 8px 15px; border-radius: 3px; cursor: pointer;">Отмена</button>
                </div>
            </form>
        </div>
    </div>

    <script>
        const token = localStorage.getItem("authToken") || localStorage.getItem("token");
        if (!token) {
            console.error("Token not found in localStorage");
            window.location.href = "/auth/login";
        }
        const urlParams = new URLSearchParams(window.location.search);
        const isFamilyView = urlParams.get("family") === "true";
        const accountsUrl = "/accounts" + (isFamilyView ? "?family=true" : "");
        const accountByIdUrl = (id) => "/accounts/" + id + (isFamilyView ? "?family=true" : "");
        console.log("Token found:", token ? "Yes" : "No", "Family view:", isFamilyView);

        const accountTypeNames = {
            cash: "Наличные",
            card: "Карта",
            deposit: "Депозит"
        };

        function formatDate(dateStr) {
            if (!dateStr) return "-";
            try {
                const date = new Date(dateStr);
                return date.toLocaleString("ru-RU", {
                    year: "numeric",
                    month: "2-digit",
                    day: "2-digit",
                    hour: "2-digit",
                    minute: "2-digit"
                });
            } catch (e) {
                return dateStr || "-";
            }
        }

        function escapeHtml(text) {
            const div = document.createElement("div");
            div.textContent = text;
            return div.innerHTML;
        }

        async function loadAccounts() {
            const loadingMsg = document.getElementById("loadingMessage");
            const emptyMsg = document.getElementById("emptyMessage");
            const table = document.getElementById("accountsTable");
            const tbody = document.getElementById("accountsTableBody");
            
            try {
                if (!token) {
                    loadingMsg.textContent = "Ошибка: токен авторизации не найден";
                    console.error("Token not found");
                    return;
                }
                
                console.log("Fetching accounts with token:", token ? (token.substring(0, 20) + "...") : "null");
                const resp = await fetch(accountsUrl, {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                console.log("Response status:", resp.status);
                if (!resp.ok) {
                    const errorText = await resp.text();
                    loadingMsg.textContent = "Ошибка загрузки счетов: " + (errorText || resp.status);
                    console.error("Failed to load accounts:", resp.status, errorText);
                    return;
                }
                let data;
                try {
                    data = await resp.json();
                } catch (e) {
                    const text = await resp.text();
                    loadingMsg.textContent = "Ошибка парсинга JSON: " + text;
                    console.error("JSON parse error:", e, text);
                    return;
                }
                console.log("Loaded accounts:", JSON.stringify(data));
                console.log("Accounts count:", data ? data.length : 0);
                loadingMsg.style.display = "none";
                
                if (!data) {
                    loadingMsg.textContent = "Ошибка: данные не получены";
                    console.error("No data received");
                    return;
                }
                if (!Array.isArray(data)) {
                    loadingMsg.textContent = "Ошибка: неверный формат данных";
                    console.error("Data is not an array:", typeof data, JSON.stringify(data));
                    return;
                }
                console.log("Data is array, length:", data.length);
                if (data.length === 0) {
                    console.log("No accounts found, showing empty message");
                    emptyMsg.style.display = "block";
                    table.style.display = "none";
                    loadingMsg.style.display = "none";
                } else {
                    console.log("Found", data.length, "accounts, displaying table");
                    emptyMsg.style.display = "none";
                    table.style.display = "table";
                    tbody.innerHTML = "";
                    data.forEach((acc, index) => {
                        console.log("Processing account", index + 1, ":", JSON.stringify(acc));
                        const row = document.createElement("tr");
                        row.style.borderBottom = "1px solid #eee";
                        const typeName = accountTypeNames[acc.account_type] || acc.account_type;
                        const accessType = acc.is_family ? "Семейный" : "Личный";
                        row.innerHTML = "<td style=\"word-break: break-word; white-space: normal;\">" + escapeHtml(acc.account_name) + "</td>" +
                            "<td>" + escapeHtml(typeName) + "</td>" +
                            "<td style=\"font-weight: bold;\">" + escapeHtml(acc.balance) + " руб.</td>" +
                            "<td>" + accessType + "</td>" +
                            "<td>" + formatDate(acc.created_at) + "</td>" +
                            "<td class=\"actions\">" +
                            "<button onclick=\"editAccount(" + acc.id + ", '" + escapeHtml(acc.account_name) + "', '" + acc.account_type + "', '" + acc.balance + "')\">Редактировать</button>" +
                            "<button onclick=\"deleteAccount(" + acc.id + ")\">Удалить</button>" +
                            "</td>";
                        tbody.appendChild(row);
                        console.log("Account row added to table");
                    });
                    console.log("All accounts displayed");
                }
            } catch (error) {
                loadingMsg.textContent = "Ошибка сети: " + error.message;
                console.error("Error loading accounts:", error);
            }
        }

        function editAccount(id, name, type, balance) {
            document.getElementById("editAccountId").value = id;
            document.getElementById("editAccountName").value = name;
            document.getElementById("editAccountType").value = type;
            document.getElementById("editAccountBalance").value = balance;
            document.getElementById("editModal").style.display = "block";
        }

        function closeEditModal() {
            document.getElementById("editModal").style.display = "none";
        }

        async function updateAccount() {
            const id = document.getElementById("editAccountId").value;
            const name = document.getElementById("editAccountName").value;
            const type = document.getElementById("editAccountType").value;
            const balance = document.getElementById("editAccountBalance").value;

            if (!name || !name.trim()) {
                alert("Название счёта не может быть пустым");
                return;
            }

            if (parseFloat(balance) < 0) {
                alert("Баланс не может быть отрицательным");
                return;
            }

            try {
                const resp = await fetch(accountByIdUrl(id), {
                    method: "PUT",
                    headers: {
                        "Content-Type": "application/json",
                        "Authorization": "Bearer " + token
                    },
                    body: JSON.stringify({
                        account_name: name,
                        account_type: type,
                        balance: balance
                    })
                });
                const text = await resp.text();
                if (resp.ok) {
                    alert("Счёт обновлён");
                    closeEditModal();
                    loadAccounts();
                } else {
                    alert("Ошибка: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        }

        document.getElementById("editAccountForm").addEventListener("submit", async (e) => {
            e.preventDefault();
            await updateAccount();
        });

        // Закрытие модального окна при клике вне его
        window.onclick = function(event) {
            const modal = document.getElementById("editModal");
            if (event.target == modal) {
                closeEditModal();
            }
        }

        async function deleteAccount(id) {
            if (!confirm("Вы уверены, что хотите удалить этот счёт?")) {
                return;
            }
            try {
                const resp = await fetch(accountByIdUrl(id), {
                    method: "DELETE",
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (resp.ok) {
                    alert("Счёт удалён");
                    loadAccounts();
                } else {
                    const text = await resp.text();
                    alert("Ошибка: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        }

        document.getElementById("accountForm").addEventListener("submit", async (e) => {
            e.preventDefault();
            const form = e.target;
            const body = {
                account_name: form.account_name.value,
                account_type: form.account_type.value,
                balance: form.balance.value || "0"
            };
            try {
                const resp = await fetch(accountsUrl, {
                    method: "POST",
                    headers: {
                        "Content-Type": "application/json",
                        "Authorization": "Bearer " + token
                    },
                    body: JSON.stringify(body)
                });
                const text = await resp.text();
                if (resp.ok) {
                    alert("Счёт создан");
                    form.reset();
                    form.balance.value = "0";
                    loadAccounts();
                } else {
                    alert("Ошибка: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        });

        // Загружаем счета при загрузке страницы
        loadAccounts();
    </script>
</body>
</html>
)HTML";
    
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    callback(resp);
}
