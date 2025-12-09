#include "PageController.h"
#include <drogon/HttpResponse.h>
#include <drogon/HttpViewData.h>
#include "utils/JwtUtils.h"
#include <drogon/HttpAppFramework.h>

using namespace finance;

// Общий стиль для всех UI-страниц (зелёная тема, скругления)
static const std::string kCommonStyles = R"HTML(
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
        input[type="text"], input[type="number"], select {
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
        .actions button:nth-child(2) {
            background: #dc3545;
        }
        .actions button:nth-child(2):hover {
            background: #b42b38;
        }
        .modal-overlay {
            display: none;
            position: fixed;
            z-index: 9999;
            left: 0; top: 0; width: 100%; height: 100%;
            background: rgba(0,0,0,0.35);
        }
        .modal-box {
            background: #fff;
            margin: 8% auto;
            padding: 20px;
            border: 1px solid var(--border);
            width: 90%;
            max-width: 520px;
            border-radius: 12px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.12);
        }
        .badge {
            display: inline-block;
            padding: 2px 8px;
            border-radius: 999px;
            background: #e8f3e7;
            color: var(--green-dark);
            font-size: 12px;
        }
    </style>
)HTML";

void PageController::Index(const drogon::HttpRequestPtr&,
                           std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    std::string html = R"(<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8" />
    <title>Financial Manager</title>
    <link rel="stylesheet" href="https://unpkg.com/sakura.css/css/sakura.css" />
</head>
<body>
    <h1>Financial Manager</h1>
    <p>Добро пожаловать! Для продолжения войдите в аккаунт или зарегистрируйтесь.</p>
    <nav>
        <ul>
            <li><a href="/auth/register">Регистрация</a></li>
            <li><a href="/auth/login">Вход</a></li>
        </ul>
    </nav>
</body>
</html>
)";
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    callback(resp);
}

drogon::Task<drogon::HttpResponsePtr> PageController::HomePage(drogon::HttpRequestPtr req) {
    std::string html = R"HTML(<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8" />
    <title>Financial Manager</title>
    <link rel="stylesheet" href="https://unpkg.com/sakura.css/css/sakura.css" />
)HTML";
    html += kCommonStyles;
    html += R"HTML(
    <style>
        .invite-notification {
            background-color: #e3f2fd;
            border: 1px solid #2196f3;
            border-radius: 8px;
            padding: 15px;
            margin: 20px 0;
        }
        .family-info {
            background-color: #f1f8e9;
            border: 1px solid #8bc34a;
            border-radius: 8px;
            padding: 15px;
            margin: 20px 0;
        }
        .section-title {
            font-weight: bold;
            margin-top: 20px;
            margin-bottom: 10px;
            color: var(--green-dark);
        }
        .loading {
            color: #666;
            font-style: italic;
        }
        .card {
            background: #fff;
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 16px;
            box-shadow: 0 6px 20px rgba(0,0,0,0.08);
            margin-bottom: 16px;
        }
        .nav-btn {
            background: var(--green);
            color: #fff;
            border: none;
            border-radius: 8px;
            padding: 8px 12px;
            cursor: pointer;
            text-decoration: none;
            transition: all 0.15s ease;
            display: inline-block;
        }
        .nav-btn:hover { background: var(--green-dark); transform: translateY(-1px); }
        nav ul { list-style: none; padding: 0; }
        nav li { margin: 6px 0; }
    </style>
</head>
<body>
    <h1>Выбор действия</h1>
    <div id="loadingMessage" class="loading">Загрузка...</div>
    <div id="contentArea"></div>
    
    <script>
        const token = localStorage.getItem("authToken") || localStorage.getItem("token");
        if (!token) {
            document.getElementById("loadingMessage").textContent = "Вы не авторизованы. Пожалуйста, войдите в систему.";
            setTimeout(() => {
                window.location.href = "/auth/login";
            }, 2000);
        } else {
            loadHomeContent();
        }
        
        async function loadHomeContent() {
            try {
                // Загружаем информацию о семье
                const familyResp = await fetch("/api/family", {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                
                // Загружаем приглашения
                const invitesResp = await fetch("/api/family/invites", {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                
                let html = "";
                
                // Обрабатываем приглашения
                if (invitesResp.ok) {
                    const invites = await invitesResp.json();
                    if (invites && invites.length > 0) {
                        html += "<div class=\"invite-notification\">";
                        html += "<strong>У вас есть " + invites.length + " приглашение(й) в семью!</strong>";
                        html += "<p>";
                        invites.forEach(invite => {
                            if (invite.token) {
                                html += "<a href=\"/join-family?token=" + invite.token + "\" style=\"display: inline-block; margin: 5px; padding: 12px 24px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 5px; font-weight: bold; font-size: 16px;\">✅ Принять приглашение в " + (invite.family_name || "Семья") + "</a>";
                            }
                        });
                        html += "</p>";
                        html += "</div>";
                    }
                }
                
                // Обрабатываем информацию о семье
                if (familyResp.ok) {
                    const family = await familyResp.json();
                    if (family && family.id) {
                        html += "<div class=\"family-info\">";
                        html += "<strong>Семья: " + family.name + "</strong>";
                        html += "<p>";
                        html += "<a href=\"/family/members\" style=\"margin-right: 15px;\">Просмотреть членов семьи</a>";
                        html += "<a href=\"/family/invite\" style=\"background-color: #2196f3; color: white; padding: 12px 24px; text-decoration: none; border-radius: 5px; display: inline-block; font-weight: bold; font-size: 16px;\">➕ Пригласить в семью</a>";
                        html += "</p>";
                        html += "</div>";
                        
                        html += "<div class=\"section-title\">Личные финансы:</div>";
                        html += "<nav><ul>";
                        html += "<li><a href=\"/accounts/create\">Счета</a></li>";
                        html += "<li><a href=\"/ui/categories\">Категории</a></li>";
                        html += "<li><a href=\"/ui/transactions\">Транзакции</a></li>";
                        html += "<li><a href=\"/ui/budgets\">Бюджеты</a></li>";
                        html += "<li><a href=\"/ui/transfers\">Переводы</a></li>";
                        html += "</ul></nav>";
                        
                        html += "<div class=\"section-title\">Семейные финансы:</div>";
                        html += "<nav><ul>";
                        html += "<li><a href=\"/accounts/create?family=true\">Семейные счета</a></li>";
                        html += "<li><a href=\"/ui/categories?family=true\">Семейные категории</a></li>";
                        html += "<li><a href=\"/ui/transactions?family=true\">Семейные транзакции</a></li>";
                        html += "<li><a href=\"/ui/budgets?family=true\">Семейные бюджеты</a></li>";
                        html += "<li><a href=\"/ui/transfers?family=true\">Семейные переводы</a></li>";
                        html += "</ul></nav>";
                    } else {
                        html += "<div style=\"margin: 20px 0;\">";
                        html += "<a href=\"/family/create\" style=\"background-color: #4CAF50; color: white; padding: 10px 20px; text-decoration: none; border-radius: 5px; display: inline-block;\">";
                        html += "Создать семейный аккаунт</a>";
                        html += "</div>";
                        
                        html += "<div class=\"section-title\">Личные финансы:</div>";
                        html += "<nav><ul>";
                        html += "<li><a href=\"/accounts/create\">Счета</a></li>";
                        html += "<li><a href=\"/ui/categories\">Категории</a></li>";
                        html += "<li><a href=\"/ui/transactions\">Транзакции</a></li>";
                        html += "<li><a href=\"/ui/budgets\">Бюджеты</a></li>";
                        html += "<li><a href=\"/ui/transfers\">Переводы</a></li>";
                        html += "</ul></nav>";
                    }
                } else {
                    html += "<div style=\"margin: 20px 0;\">";
                    html += "<a href=\"/family/create\" style=\"background-color: #4CAF50; color: white; padding: 10px 20px; text-decoration: none; border-radius: 5px; display: inline-block;\">";
                    html += "Создать семейный аккаунт</a>";
                    html += "</div>";
                    
                    html += "<div class=\"section-title\">Личные финансы:</div>";
                    html += "<nav><ul>";
                    html += "<li><a href=\"/accounts/create\">Счета</a></li>";
                    html += "<li><a href=\"/ui/categories\">Категории</a></li>";
                    html += "<li><a href=\"/ui/transactions\">Транзакции</a></li>";
                    html += "<li><a href=\"/ui/budgets\">Бюджеты</a></li>";
                    html += "<li><a href=\"/ui/transfers\">Переводы</a></li>";
                    html += "</ul></nav>";
                }
                
                document.getElementById("loadingMessage").style.display = "none";
                document.getElementById("contentArea").innerHTML = html;
            } catch (error) {
                document.getElementById("loadingMessage").textContent = "Ошибка загрузки данных: " + error.message;
                console.error("Error loading home content:", error);
            }
        }
    </script>
    <nav style="margin-top: 30px;">
        <ul>
            <li><a href="/auth/logout">Выход из аккаунта</a></li>
        </ul>
    </nav>
</body>
</html>
)HTML";
    
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    co_return resp;
}

void PageController::CategoriesPage(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    bool isFamily = req->getParameter("family") == "true";
    std::string pageTitle = isFamily ? "Семейные категории" : "Категории";
    
    std::string html;
    html += "<!DOCTYPE html>\n";
    html += "<html lang=\"ru\">\n<head>\n    <meta charset=\"UTF-8\" />\n    <title>" + pageTitle + " - Financial Manager</title>\n";
    html += "    <link rel=\"stylesheet\" href=\"https://unpkg.com/sakura.css/css/sakura.css\" />\n";
    html += kCommonStyles;
    html += "</head>\n<body>\n    <h1>" + pageTitle + "</h1>\n";
    
    {
        html += R"(    <form id="createCategoryForm">
        <label>Название
            <input type="text" name="name" required />
        </label>
        <label>Тип
            <select name="type" required>
                <option value="income">Доход</option>
                <option value="expense">Расход</option>
            </select>
        </label>
        <button type="submit">Создать</button>
        <button type="button" id="cancelCategoryEdit" style="display:none; margin-left: 8px;">Отмена</button>
    </form>
)";
    }
    
    html += R"HTML(    <p><a href="/home">← Вернуться на главную</a></p>

    <h2>Список категорий</h2>
    <div id="categoriesContainer">
        <p id="loadingMessage">Загрузка...</p>
        <div id="emptyMessage" style="display: none;">
            <p style="color: #666; font-style: italic;">Пока не было добавлено ни одной категории</p>
        </div>
        <table id="categoriesTable" style="display: none; width: 100%; border-collapse: collapse; margin-top: 1em;">
            <thead>
                <tr style="background-color: #f0f0f0;">
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Название</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Тип</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Тип доступа</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Действия</th>
                </tr>
            </thead>
            <tbody id="categoriesTableBody">
            </tbody>
        </table>
    </div>

    <div id="editCategoryModal" style="display:none; position:fixed; z-index:9999; left:0; top:0; width:100%; height:100%; overflow:auto; background-color: rgba(0,0,0,0.4);">
        <div style="background:#fff; margin:10% auto; padding:20px; border:1px solid #888; width:90%; max-width:480px; border-radius:6px;">
            <h3>Редактировать категорию</h3>
            <form id="editCategoryForm">
                <label>Название
                    <input type="text" id="editCategoryName" required />
                </label>
                <label>Тип
                    <select id="editCategoryType" required>
                        <option value="income">Доход</option>
                        <option value="expense">Расход</option>
                    </select>
                </label>
                <div style="margin-top:12px;">
                    <button type="submit">Сохранить</button>
                    <button type="button" onclick="closeEditCategoryModal()" style="margin-left:8px;">Отмена</button>
                </div>
            </form>
        </div>
    </div>

    <script>
        const token = localStorage.getItem("authToken") || localStorage.getItem("token");
        if (!token) {
            window.location.href = "/auth/login";
        }
        const urlParams = new URLSearchParams(window.location.search);
        const isFamily = urlParams.get("family") === "true";
        
        const categoriesUrl = "/categories" + (isFamily ? "?family=true" : "");

        let editingCategoryId = null;
        let categoriesData = [];
        const editCategoryModal = document.getElementById("editCategoryModal");
        const editCategoryName = document.getElementById("editCategoryName");
        const editCategoryType = document.getElementById("editCategoryType");

        async function loadCategories() {
            const loadingMsg = document.getElementById("loadingMessage");
            const emptyMsg = document.getElementById("emptyMessage");
            const table = document.getElementById("categoriesTable");
            const tbody = document.getElementById("categoriesTableBody");
            
            try {
                const resp = await fetch(categoriesUrl, {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (!resp.ok) {
                    loadingMsg.textContent = "Ошибка загрузки категорий";
                    return;
                }
                const data = await resp.json();
                categoriesData = data;
                loadingMsg.style.display = "none";
                
                if (!data || data.length === 0) {
                    emptyMsg.style.display = "block";
                    table.style.display = "none";
                } else {
                    emptyMsg.style.display = "none";
                    table.style.display = "table";
                    tbody.innerHTML = "";
                    data.forEach(cat => {
                        const row = document.createElement("tr");
                        row.style.borderBottom = "1px solid #eee";
                        const typeText = cat.type === "income" ? "Доход" : "Расход";
                        const typeColor = cat.type === "income" ? "#28a745" : "#dc3545";
                        const accessType = cat.is_family ? "Семейная" : "Личная";
                        row.innerHTML = "<td style=\"padding: 0.5em; word-break: break-word; white-space: normal;\">" + escapeHtml(cat.name) + "</td>" +
                            "<td style=\"padding: 0.5em; color: " + typeColor + "; font-weight: bold;\">" + typeText + "</td>" +
                            "<td style=\"padding: 0.5em;\">" + accessType + "</td>" +
                            "<td style=\"padding: 0.5em; display: flex; gap: 6px; flex-wrap: wrap;\">" +
                                "<button onclick=\"startEditCategory(" + cat.id + ")\" style=\"padding: 4px 8px;\">Редактировать</button>" +
                                "<button onclick=\"deleteCategory(" + cat.id + ")\" style=\"padding: 4px 8px; background:#dc3545; color:#fff; border:none;\">Удалить</button>" +
                            "</td>";
                        tbody.appendChild(row);
                    });
                }
            } catch (error) {
                loadingMsg.textContent = "Ошибка сети: " + error.message;
                console.error("Error loading categories:", error);
            }
        }

        function escapeHtml(text) {
            const div = document.createElement("div");
            div.textContent = text;
            return div.innerHTML;
        }

        const form = document.getElementById("createCategoryForm");
        if (form) {
            form.addEventListener("submit", async (e) => {
                e.preventDefault();
                const body = {
                    name: form.name.value,
                    type: form.type.value
                };
                try {
                    const url = editingCategoryId ? ("/categories/" + editingCategoryId + (isFamily ? "?family=true" : "")) : categoriesUrl;
                    const method = editingCategoryId ? "PUT" : "POST";
                    const resp = await fetch(url, {
                        method,
                        headers: {
                            "Content-Type": "application/json",
                            "Authorization": "Bearer " + token
                        },
                        body: JSON.stringify(body)
                    });
                    const text = await resp.text();
                    if (resp.ok) {
                        alert(editingCategoryId ? "Категория обновлена" : "Категория создана");
                        form.reset();
                        editingCategoryId = null;
                        form.querySelector("button[type='submit']").textContent = "Создать";
                        const cancelBtn = document.getElementById("cancelCategoryEdit");
                        if (cancelBtn) cancelBtn.style.display = "none";
                        loadCategories();
                    } else {
                        alert("Ошибка: " + text);
                    }
                } catch (error) {
                    alert("Ошибка сети: " + error.message);
                }
            });
        }
        const cancelBtnCat = document.getElementById("cancelCategoryEdit");
        if (cancelBtnCat) {
            cancelBtnCat.addEventListener("click", cancelCategoryEdit);
        }

        function startEditCategory(id) {
            const cat = categoriesData.find(c => c.id === id);
            if (!cat) return;
            editingCategoryId = id;
            editCategoryName.value = cat.name;
            editCategoryType.value = cat.type;
            editCategoryModal.style.display = "block";
        }

        function cancelCategoryEdit() {
            const form = document.getElementById("createCategoryForm");
            form.reset();
            editingCategoryId = null;
            form.querySelector("button[type='submit']").textContent = "Создать";
            document.getElementById("cancelCategoryEdit").style.display = "none";
            closeEditCategoryModal();
        }

        function closeEditCategoryModal() {
            editCategoryModal.style.display = "none";
            editingCategoryId = null;
        }

        document.getElementById("editCategoryForm").addEventListener("submit", async (e) => {
            e.preventDefault();
            if (!editingCategoryId) {
                closeEditCategoryModal();
                return;
            }
            try {
                const resp = await fetch("/categories/" + editingCategoryId + (isFamily ? "?family=true" : ""), {
                    method: "PUT",
                    headers: {
                        "Content-Type": "application/json",
                        "Authorization": "Bearer " + token
                    },
                    body: JSON.stringify({
                        name: editCategoryName.value,
                        type: editCategoryType.value
                    })
                });
                const text = await resp.text();
                if (resp.ok) {
                    alert("Категория обновлена");
                    closeEditCategoryModal();
                    loadCategories();
                } else {
                    alert("Ошибка: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        });

        async function deleteCategory(id) {
            if (!confirm("Удалить категорию?")) return;
            try {
                const resp = await fetch("/categories/" + id + (isFamily ? "?family=true" : ""), {
                    method: "DELETE",
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (resp.status === 204) {
                    if (editingCategoryId === id) {
                        cancelCategoryEdit();
                    }
                    loadCategories();
                } else {
                    const text = await resp.text();
                    alert("Ошибка удаления: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        }

        // Загружаем категории при загрузке страницы
        loadCategories();
    </script>
</body>
</html>
)HTML";
    
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    callback(resp);
}

void PageController::TransactionsPage(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    bool isFamily = req->getParameter("family") == "true";
    std::string pageTitle = isFamily ? "Семейные транзакции" : "Транзакции";
    
    // Читаем файл transactions.csp и генерируем HTML на его основе
    // Для краткости, создам упрощенную версию
    std::string html;
    html += "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n    <meta charset=\"UTF-8\" />\n    <title>" + pageTitle + " - Financial Manager</title>\n";
    html += "    <link rel=\"stylesheet\" href=\"https://unpkg.com/sakura.css/css/sakura.css\" />\n";
    html += kCommonStyles;
    html += "</head>\n<body>\n    <h1>" + pageTitle + "</h1>\n";
    
    {
        html += R"(    <form id="createTransactionForm">
        <label>Счёт
            <select name="id_account" id="accountSelect" required>
                <option value="">Загрузка счетов...</option>
            </select>
            <small id="accountsError" style="color: red; display: none;">Сначала создайте хотя бы один счёт</small>
        </label>
        <label>Категория
            <select name="id_category" id="categorySelect">
                <option value="">Загрузка категорий...</option>
            </select>
            <small style="color: #666; font-size: 0.9em;">(необязательно)</small>
        </label>
        <label>Сумма
            <input type="number" name="amount" step="0.01" min="0.01" required />
        </label>
        <label>Тип
            <select name="type" required>
                <option value="income">Доход</option>
                <option value="expense">Расход</option>
            </select>
        </label>
        <label>Описание
            <input type="text" name="description" />
        </label>
        <button type="submit" id="submitBtn">Создать</button>
        <button type="button" id="cancelTransactionEdit" style="display:none; margin-left:8px;">Отмена</button>
    </form>
)";
    }
    
    html += R"HTML(    <p><a href="/home">← Вернуться на главную</a></p>

    <h2>Список транзакций</h2>
    <div id="transactionsContainer">
        <p id="loadingMessage">Загрузка...</p>
        <div id="emptyMessage" style="display: none;">
            <p style="color: #666; font-style: italic;">Пока не было добавлено ни одной транзакции</p>
        </div>
        <table id="transactionsTable" style="display: none; width: 100%; border-collapse: collapse; margin-top: 1em;">
            <thead>
                <tr style="background-color: #f0f0f0;">
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Счёт</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Сумма</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Тип</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Категория</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Описание</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Дата</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Доступ</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Действия</th>
                </tr>
            </thead>
            <tbody id="transactionsTableBody">
            </tbody>
        </table>
    </div>

    <div id="editTransactionModal" style="display:none; position:fixed; z-index:9999; left:0; top:0; width:100%; height:100%; overflow:auto; background-color: rgba(0,0,0,0.4);">
        <div style="background:#fff; margin:6% auto; padding:20px; border:1px solid #888; width:90%; max-width:520px; border-radius:6px;">
            <h3>Редактировать транзакцию</h3>
            <form id="editTransactionForm">
                <label>Счёт
                    <select id="editTxAccount" required></select>
                </label>
                <label>Категория
                    <select id="editTxCategory">
                        <option value="">Не выбрано</option>
                    </select>
                </label>
                <label>Сумма
                    <input type="number" id="editTxAmount" step="0.01" min="0.01" required />
                </label>
                <label>Тип
                    <select id="editTxType" required>
                        <option value="income">Доход</option>
                        <option value="expense">Расход</option>
                    </select>
                </label>
                <label>Описание
                    <input type="text" id="editTxDescription" />
                </label>
                <div style="margin-top:12px;">
                    <button type="submit">Сохранить</button>
                    <button type="button" onclick="closeEditTransactionModal()" style="margin-left:8px;">Отмена</button>
                </div>
            </form>
        </div>
    </div>

    <script>
        const token = localStorage.getItem("authToken") || localStorage.getItem("token");
        if (!token) {
            window.location.href = "/auth/login";
        }
        const urlParams = new URLSearchParams(window.location.search);
        const isFamilyView = urlParams.get("family") === "true";
        
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

        let accountsCache = {};
        let categoriesCacheTx = {};
        let editingTransactionId = null;
        let transactionsData = [];
        const editTxModal = document.getElementById("editTransactionModal");
        const editTxAccount = document.getElementById("editTxAccount");
        const editTxCategory = document.getElementById("editTxCategory");
        const editTxAmount = document.getElementById("editTxAmount");
        const editTxType = document.getElementById("editTxType");
        const editTxDescription = document.getElementById("editTxDescription");

        async function loadAccountsForDisplay() {
            try {
                const resp = await fetch("/accounts" + (isFamilyView ? "?family=true" : ""), {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (resp.ok) {
                    const accounts = await resp.json();
                    accountsCache = {};
                    accounts.forEach(acc => {
                        accountsCache[acc.id] = acc;
                    });
                    fillTxAccountSelects(accounts);
                }
            } catch {}
        }

        function fillTxAccountSelects(accounts) {
            const options = ['<option value="">Выберите счёт</option>'].concat(
                accounts.map(acc => `<option value="${acc.id}">${escapeHtml(acc.account_name)} (${acc.account_type}) - Баланс: ${acc.balance} руб.</option>`)
            ).join("");
            if (editTxAccount) {
                editTxAccount.innerHTML = options;
            }
        }

        async function loadTransactions() {
            const loadingMsg = document.getElementById("loadingMessage");
            const emptyMsg = document.getElementById("emptyMessage");
            const table = document.getElementById("transactionsTable");
            const tbody = document.getElementById("transactionsTableBody");
            
            try {
                const url = "/transactions" + (isFamilyView ? "?family=true" : "");
                const resp = await fetch(url, {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (!resp.ok) {
                    loadingMsg.textContent = "Ошибка загрузки транзакций";
                    return;
                }
                const data = await resp.json();
                transactionsData = data;
                loadingMsg.style.display = "none";
                
                if (!data || data.length === 0) {
                    emptyMsg.style.display = "block";
                    table.style.display = "none";
                } else {
                    emptyMsg.style.display = "none";
                    table.style.display = "table";
                    tbody.innerHTML = "";
                    data.forEach(tr => {
                        const row = document.createElement("tr");
                        row.style.borderBottom = "1px solid #eee";
                        const typeText = tr.type === "income" ? "Доход" : "Расход";
                        const typeColor = tr.type === "income" ? "#28a745" : "#dc3545";
                        const amountColor = tr.type === "income" ? "#28a745" : "#dc3545";
                        const account = accountsCache[tr.id_account];
                        const accountName = account ? account.account_name : "Счёт недоступен";
                        const category = tr.id_category ? categoriesCacheTx[tr.id_category] : null;
                        const categoryName = category ? category.name : "-";
                        const amountSign = tr.type === "income" ? "+" : "-";
                        const accessType = tr.is_family ? "Семейная" : "Личная";
                        row.innerHTML =
                            "<td style=\"padding: 0.5em; word-break: break-word; white-space: normal;\">" + escapeHtml(accountName) + "</td>" +
                            "<td style=\"padding: 0.5em; color: " + amountColor + "; font-weight: bold;\">" + amountSign + escapeHtml(tr.amount) + " руб.</td>" +
                            "<td style=\"padding: 0.5em; color: " + typeColor + "; font-weight: bold;\">" + typeText + "</td>" +
                            "<td style=\"padding: 0.5em; word-break: break-word; white-space: normal;\">" + escapeHtml(categoryName) + "</td>" +
                            "<td style=\"padding: 0.5em; word-break: break-word; white-space: normal;\">" + escapeHtml(tr.description || "-") + "</td>" +
                            "<td style=\"padding: 0.5em;\">" + formatDate(tr.created_at) + "</td>" +
                            "<td style=\"padding: 0.5em;\">" + accessType + "</td>" +
                            "<td style=\"padding: 0.5em; display:flex; gap:6px; flex-wrap:wrap;\">" +
                                "<button onclick=\"startEditTransaction(" + tr.id + ")\" style=\"padding:4px 8px;\">Редактировать</button>" +
                                "<button onclick=\"deleteTransaction(" + tr.id + ")\" style=\"padding:4px 8px; background:#dc3545; color:#fff; border:none;\">Удалить</button>" +
                            "</td>";
                        tbody.appendChild(row);
                    });
                }
            } catch (error) {
                loadingMsg.textContent = "Ошибка сети: " + error.message;
                console.error("Error loading transactions:", error);
            }
        }

        function escapeHtml(text) {
            const div = document.createElement("div");
            div.textContent = text;
            return div.innerHTML;
        }

        async function loadAccounts() {
            const select = document.getElementById("accountSelect");
            const errorMsg = document.getElementById("accountsError");
            const submitBtn = document.getElementById("submitBtn");
            
            try {
                const resp = await fetch("/accounts" + (isFamilyView ? "?family=true" : ""), {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (!resp.ok) {
                    select.innerHTML = "<option value=\"\">Ошибка загрузки счетов</option>";
                    return;
                }
                const accounts = await resp.json();
                select.innerHTML = "";
                
                if (!accounts || accounts.length === 0) {
                    select.innerHTML = "<option value=\"\">Нет доступных счетов</option>";
                    select.disabled = true;
                    errorMsg.style.display = "block";
                    submitBtn.disabled = true;
                } else {
                    errorMsg.style.display = "none";
                    submitBtn.disabled = false;
                    select.disabled = false;
                    select.innerHTML = "<option value=\"\">Выберите счёт</option>";
                    accounts.forEach(acc => {
                        const option = document.createElement("option");
                        option.value = acc.id;
                        option.textContent = acc.account_name + " (" + acc.account_type + ") - Баланс: " + acc.balance + " руб.";
                        select.appendChild(option);
                    });
                }
            } catch (error) {
                select.innerHTML = "<option value=\"\">Ошибка сети</option>";
                console.error("Error loading accounts:", error);
            }
        }

        async function loadCategories() {
            const select = document.getElementById("categorySelect");
            
            try {
                const resp = await fetch("/categories" + (isFamilyView ? "?family=true" : ""), {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (!resp.ok) {
                    select.innerHTML = "<option value=\"\">Ошибка загрузки категорий</option>";
                    return;
                }
                const categories = await resp.json();
                categoriesCacheTx = {};
                categories.forEach(cat => { categoriesCacheTx[cat.id] = cat; });
                select.innerHTML = "<option value=\"\">Не выбрано</option>";
                
                if (categories && categories.length > 0) {
                    categories.forEach(cat => {
                        const option = document.createElement("option");
                        option.value = cat.id;
                        const typeText = cat.type === "income" ? "Доход" : "Расход";
                        option.textContent = cat.name + " (" + typeText + ")";
                        select.appendChild(option);
                    });
                }
                // дублируем опции в модалку
                if (editTxCategory) {
                    editTxCategory.innerHTML = select.innerHTML;
                }
            } catch (error) {
                select.innerHTML = "<option value=\"\">Ошибка сети</option>";
                console.error("Error loading categories:", error);
            }
        }

        const form = document.getElementById("createTransactionForm");
        if (form) {
            form.addEventListener("submit", async (e) => {
                e.preventDefault();
                const accountId = form.id_account.value;
                if (!accountId) {
                    alert("Пожалуйста, выберите счёт");
                    return;
                }
                const amountValue = parseFloat(form.amount.value);
                if (isNaN(amountValue) || amountValue <= 0) {
                    alert("Пожалуйста, введите корректную сумму (больше 0)");
                    return;
                }
                
                const body = {
                    id_account: Number(accountId),
                    amount: amountValue.toString(),
                    type: form.type.value,
                    description: form.description.value || ""
                };
                if (form.id_category.value) {
                    body.id_category = Number(form.id_category.value);
                }
                try {
                    const url = editingTransactionId
                        ? ("/transactions/" + editingTransactionId + (isFamilyView ? "?family=true" : ""))
                        : ("/transactions" + (isFamilyView ? "?family=true" : ""));
                    const resp = await fetch(url, {
                        method: editingTransactionId ? "PUT" : "POST",
                        headers: {
                            "Content-Type": "application/json",
                            "Authorization": "Bearer " + token
                        },
                        body: JSON.stringify(body)
                    });
                    const text = await resp.text();
                    if (resp.ok) {
                        alert(editingTransactionId ? "Транзакция обновлена" : "Транзакция создана");
                        form.reset();
                        form.id_category.value = "";
                        editingTransactionId = null;
                        document.getElementById("submitBtn").textContent = "Создать";
                        document.getElementById("cancelTransactionEdit").style.display = "none";
                        loadAccounts();
                        loadCategories();
                        loadAccountsForDisplay().then(() => loadTransactions());
                    } else {
                        alert("Ошибка: " + text);
                    }
                } catch (error) {
                    alert("Ошибка сети: " + error.message);
                }
            });
        }

        const cancelBtnTx = document.getElementById("cancelTransactionEdit");
        if (cancelBtnTx) {
            cancelBtnTx.addEventListener("click", cancelTransactionEdit);
        }

        function startEditTransaction(id) {
            const tr = transactionsData.find(t => t.id === id);
            if (!tr) return;
            editingTransactionId = id;
            editTxAccount.value = tr.id_account;
            editTxAmount.value = tr.amount;
            editTxType.value = tr.type;
            editTxDescription.value = tr.description || "";
            editTxCategory.value = tr.id_category || "";
            editTxModal.style.display = "block";
        }

        function cancelTransactionEdit() {
            editingTransactionId = null;
            editTxModal.style.display = "none";
        }

        function closeEditTransactionModal() {
            editingTransactionId = null;
            editTxModal.style.display = "none";
        }

        document.getElementById("editTransactionForm").addEventListener("submit", async (e) => {
            e.preventDefault();
            if (!editingTransactionId) {
                closeEditTransactionModal();
                return;
            }
            const accountId = editTxAccount.value;
            if (!accountId) {
                alert("Пожалуйста, выберите счёт");
                return;
            }
            const amountValue = parseFloat(editTxAmount.value);
            if (isNaN(amountValue) || amountValue <= 0) {
                alert("Пожалуйста, введите корректную сумму (больше 0)");
                return;
            }
            const body = {
                id_account: Number(accountId),
                amount: amountValue.toString(),
                type: editTxType.value,
                description: editTxDescription.value || ""
            };
            if (editTxCategory.value) {
                body.id_category = Number(editTxCategory.value);
            }
            try {
                const url = "/transactions/" + editingTransactionId + (isFamilyView ? "?family=true" : "");
                const resp = await fetch(url, {
                    method: "PUT",
                    headers: {
                        "Content-Type": "application/json",
                        "Authorization": "Bearer " + token
                    },
                    body: JSON.stringify(body)
                });
                const text = await resp.text();
                if (resp.ok) {
                    alert("Транзакция обновлена");
                    closeEditTransactionModal();
                    loadAccountsForDisplay().then(() => loadTransactions());
                } else {
                    alert("Ошибка: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        });

        async function deleteTransaction(id) {
            if (!confirm("Удалить транзакцию?")) return;
            try {
                const resp = await fetch("/transactions/" + id + (isFamilyView ? "?family=true" : ""), {
                    method: "DELETE",
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (resp.status === 204) {
                    if (editingTransactionId === id) {
                        cancelTransactionEdit();
                    }
                    loadAccountsForDisplay().then(() => loadTransactions());
                } else {
                    const text = await resp.text();
                    alert("Ошибка удаления: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        }

        // Загружаем счета, категории и транзакции при загрузке страницы
        loadAccounts();
        loadCategories();
        loadAccountsForDisplay().then(() => loadTransactions());
    </script>
</body>
</html>
)HTML";
    
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    callback(resp);
}

void PageController::TransfersPage(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    bool isFamily = req->getParameter("family") == "true";
    std::string pageTitle = isFamily ? "Семейные переводы" : "Переводы";
    
    std::string html;
    html += "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n    <meta charset=\"UTF-8\" />\n    <title>" + pageTitle + " - Financial Manager</title>\n";
    html += "    <link rel=\"stylesheet\" href=\"https://unpkg.com/sakura.css/css/sakura.css\" />\n";
    html += kCommonStyles;
    html += "</head>\n<body>\n    <h1>" + pageTitle + "</h1>\n";
    
    {
        html += R"(    <form id="createTransferForm">
        <label>Счёт отправителя
            <select name="account_from" id="accountFromSelect" required>
                <option value="">Загрузка счетов...</option>
            </select>
        </label>
        <label>Счёт получателя
            <select name="account_to" id="accountToSelect" required>
                <option value="">Загрузка счетов...</option>
            </select>
        </label>
        <label>Сумма
            <input type="number" name="amount" step="0.01" min="0.01" required />
        </label>
        <button type="submit" id="submitBtn">Создать перевод</button>
        <button type="button" id="cancelTransferEdit" style="display:none; margin-left:8px;">Отмена</button>
    </form>
)";
    }
    
    html += R"HTML(    <p><a href="/home">← Вернуться на главную</a></p>

    <h2>Список переводов</h2>
    <div id="transfersContainer">
        <p id="loadingMessage">Загрузка...</p>
        <div id="emptyMessage" style="display: none;">
            <p style="color: #666; font-style: italic;">Пока не было добавлено ни одного перевода</p>
        </div>
        <table id="transfersTable" style="display: none; width: 100%; border-collapse: collapse; margin-top: 1em;">
            <thead>
                <tr style="background-color: #f0f0f0;">
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">От</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">К</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Сумма</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Дата</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Доступ</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Действия</th>
                </tr>
            </thead>
            <tbody id="transfersTableBody">
            </tbody>
        </table>
    </div>

    <div id="editTransferModal" style="display:none; position:fixed; z-index:9999; left:0; top:0; width:100%; height:100%; overflow:auto; background-color: rgba(0,0,0,0.4);">
        <div style="background:#fff; margin:8% auto; padding:20px; border:1px solid #888; width:90%; max-width:520px; border-radius:6px;">
            <h3>Редактировать перевод</h3>
            <form id="editTransferForm">
                <label>Счёт отправителя
                    <select id="editTransferFrom" required></select>
                </label>
                <label>Счёт получателя
                    <select id="editTransferTo" required></select>
                </label>
                <label>Сумма
                    <input type="number" id="editTransferAmount" step="0.01" min="0.01" required />
                </label>
                <div style="margin-top:12px;">
                    <button type="submit">Сохранить</button>
                    <button type="button" onclick="closeEditTransferModal()" style="margin-left:8px;">Отмена</button>
                </div>
            </form>
        </div>
    </div>

    <script>
        const token = localStorage.getItem("authToken") || localStorage.getItem("token");
        if (!token) {
            window.location.href = "/auth/login";
        }
        const urlParams = new URLSearchParams(window.location.search);
        const isFamilyView = urlParams.get("family") === "true";
        
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

        let accountsCache = {};
        let transfersData = [];
        let editingTransferId = null;
        const editTransferModal = document.getElementById("editTransferModal");
        const editTransferFrom = document.getElementById("editTransferFrom");
        const editTransferTo = document.getElementById("editTransferTo");
        const editTransferAmount = document.getElementById("editTransferAmount");

        async function loadAccountsForDisplay() {
            try {
                const resp = await fetch("/accounts" + (isFamilyView ? "?family=true" : ""), {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (resp.ok) {
                    const accounts = await resp.json();
                    accountsCache = {};
                    accounts.forEach(acc => {
                        accountsCache[acc.id] = acc;
                    });
                    fillTransferSelects(accounts);
                }
            } catch {}
        }

        function fillTransferSelects(accounts) {
            const options = ['<option value="">Выберите счёт</option>'].concat(
                accounts.map(acc => `<option value="${acc.id}">${escapeHtml(acc.account_name)} (${acc.account_type}) - Баланс: ${acc.balance} руб.</option>`)
            ).join("");
            if (editTransferFrom) editTransferFrom.innerHTML = options;
            if (editTransferTo) editTransferTo.innerHTML = options;
        }

        async function loadTransfers() {
            const loadingMsg = document.getElementById("loadingMessage");
            const emptyMsg = document.getElementById("emptyMessage");
            const table = document.getElementById("transfersTable");
            const tbody = document.getElementById("transfersTableBody");
            
            try {
                const url = "/transfers" + (isFamilyView ? "?family=true" : "");
                const resp = await fetch(url, {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (!resp.ok) {
                    loadingMsg.textContent = "Ошибка загрузки переводов";
                    return;
                }
                const data = await resp.json();
                transfersData = data;
                loadingMsg.style.display = "none";
                
                if (!data || data.length === 0) {
                    emptyMsg.style.display = "block";
                    table.style.display = "none";
                } else {
                    emptyMsg.style.display = "none";
                    table.style.display = "table";
                    tbody.innerHTML = "";
                    data.forEach(tr => {
                        const row = document.createElement("tr");
                        row.style.borderBottom = "1px solid #eee";
                        const fromAcc = accountsCache[tr.account_from];
                        const toAcc = accountsCache[tr.account_to];
                        const fromName = fromAcc ? fromAcc.account_name : "Счёт отправителя недоступен";
                        const toName = toAcc ? toAcc.account_name : "Счёт получателя недоступен";
                        const accessType = tr.is_family ? "Семейный" : "Личный";
                        row.innerHTML =
                            "<td style=\"padding: 0.5em; word-break: break-word; white-space: normal;\">" + escapeHtml(fromName) + "</td>" +
                            "<td style=\"padding: 0.5em; word-break: break-word; white-space: normal;\">" + escapeHtml(toName) + "</td>" +
                            "<td style=\"padding: 0.5em; font-weight: bold;\">" + escapeHtml(tr.amount) + " руб.</td>" +
                            "<td style=\"padding: 0.5em;\">" + formatDate(tr.created_at) + "</td>" +
                            "<td style=\"padding: 0.5em;\">" + accessType + "</td>" +
                            "<td style=\"padding: 0.5em; display:flex; gap:6px; flex-wrap:wrap;\">" +
                                "<button onclick=\"startEditTransfer(" + tr.id + ")\" style=\"padding:4px 8px;\">Редактировать</button>" +
                                "<button onclick=\"deleteTransfer(" + tr.id + ")\" style=\"padding:4px 8px; background:#dc3545; color:#fff; border:none;\">Удалить</button>" +
                            "</td>";
                        tbody.appendChild(row);
                    });
                }
            } catch (error) {
                loadingMsg.textContent = "Ошибка сети: " + error.message;
                console.error("Error loading transfers:", error);
            }
        }

        function escapeHtml(text) {
            const div = document.createElement("div");
            div.textContent = text;
            return div.innerHTML;
        }

        async function loadAccounts() {
            const fromSelect = document.getElementById("accountFromSelect");
            const toSelect = document.getElementById("accountToSelect");
            
            try {
                const resp = await fetch("/accounts" + (isFamilyView ? "?family=true" : ""), {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (!resp.ok) {
                    fromSelect.innerHTML = "<option value=\"\">Ошибка загрузки счетов</option>";
                    toSelect.innerHTML = "<option value=\"\">Ошибка загрузки счетов</option>";
                    return;
                }
                const accounts = await resp.json();
                let options = "<option value=\"\">Выберите счёт</option>";
                accounts.forEach(acc => {
                    options += "<option value=\"" + acc.id + "\">" + acc.account_name + " (" + acc.account_type + ") - Баланс: " + acc.balance + " руб.</option>";
                });
                fromSelect.innerHTML = options;
                toSelect.innerHTML = options;
                fillTransferSelects(accounts);
            } catch (error) {
                fromSelect.innerHTML = "<option value=\"\">Ошибка сети</option>";
                toSelect.innerHTML = "<option value=\"\">Ошибка сети</option>";
                console.error("Error loading accounts:", error);
            }
        }

        const form = document.getElementById("createTransferForm");
        if (form) {
            form.addEventListener("submit", async (e) => {
                e.preventDefault();
                const fromId = form.account_from.value;
                const toId = form.account_to.value;
                if (!fromId || !toId) {
                    alert("Пожалуйста, выберите оба счёта");
                    return;
                }
                if (fromId === toId) {
                    alert("Счёт отправителя и получателя не могут быть одинаковыми");
                    return;
                }
                const amountValue = parseFloat(form.amount.value);
                if (isNaN(amountValue) || amountValue <= 0) {
                    alert("Пожалуйста, введите корректную сумму (больше 0)");
                    return;
                }
                
                const body = {
                    account_from: Number(fromId),
                    account_to: Number(toId),
                    amount: amountValue.toString()
                };
                try {
                    const url = "/transfers" + (isFamilyView ? "?family=true" : "");
                    const resp = await fetch(url, {
                        method: "POST",
                        headers: {
                            "Content-Type": "application/json",
                            "Authorization": "Bearer " + token
                        },
                        body: JSON.stringify(body)
                    });
                    const text = await resp.text();
                    if (resp.ok) {
                        alert("Перевод создан");
                        form.reset();
                        loadAccounts();
                        loadAccountsForDisplay().then(() => loadTransfers());
                    } else {
                        alert("Ошибка: " + text);
                    }
                } catch (error) {
                    alert("Ошибка сети: " + error.message);
                }
            });
        }

        function startEditTransfer(id) {
            const tr = transfersData.find(t => t.id === id);
            if (!tr) return;
            editingTransferId = id;
            editTransferFrom.value = tr.account_from;
            editTransferTo.value = tr.account_to;
            editTransferAmount.value = tr.amount;
            editTransferModal.style.display = "block";
        }

        function cancelTransferEdit() {
            editingTransferId = null;
            editTransferModal.style.display = "none";
        }

        function closeEditTransferModal() {
            editingTransferId = null;
            editTransferModal.style.display = "none";
        }

        document.getElementById("editTransferForm").addEventListener("submit", async (e) => {
            e.preventDefault();
            if (!editingTransferId) {
                closeEditTransferModal();
                return;
            }
            const fromId = editTransferFrom.value;
            const toId = editTransferTo.value;
            if (!fromId || !toId) {
                alert("Пожалуйста, выберите оба счёта");
                return;
            }
            if (fromId === toId) {
                alert("Счёт отправителя и получателя не могут быть одинаковыми");
                return;
            }
            const amountValue = parseFloat(editTransferAmount.value);
            if (isNaN(amountValue) || amountValue <= 0) {
                alert("Пожалуйста, введите корректную сумму (больше 0)");
                return;
            }
            const body = {
                account_from: Number(fromId),
                account_to: Number(toId),
                amount: amountValue.toString()
            };
            try {
                const url = "/transfers/" + editingTransferId + (isFamilyView ? "?family=true" : "");
                const resp = await fetch(url, {
                    method: "PUT",
                    headers: {
                        "Content-Type": "application/json",
                        "Authorization": "Bearer " + token
                    },
                    body: JSON.stringify(body)
                });
                const text = await resp.text();
                if (resp.ok) {
                    alert("Перевод обновлён");
                    closeEditTransferModal();
                    loadAccountsForDisplay().then(() => loadTransfers());
                } else {
                    alert("Ошибка: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        });

        async function deleteTransfer(id) {
            if (!confirm("Удалить перевод?")) return;
            try {
                const resp = await fetch("/transfers/" + id + (isFamilyView ? "?family=true" : ""), {
                    method: "DELETE",
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (resp.status === 204) {
                    if (editingTransferId === id) {
                        cancelTransferEdit();
                    }
                    loadAccountsForDisplay().then(() => loadTransfers());
                } else {
                    const text = await resp.text();
                    alert("Ошибка удаления: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        }

        // Загружаем счета и переводы при загрузке страницы
        loadAccounts();
        loadAccountsForDisplay().then(() => loadTransfers());
    </script>
</body>
</html>
)HTML";
    
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    callback(resp);
}

void PageController::BudgetsPage(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    bool isFamily = req->getParameter("family") == "true";
    std::string pageTitle = isFamily ? "Семейные бюджеты" : "Бюджеты";
    
    std::string html;
    html += "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n    <meta charset=\"UTF-8\" />\n    <title>" + pageTitle + " - Financial Manager</title>\n";
    html += "    <link rel=\"stylesheet\" href=\"https://unpkg.com/sakura.css/css/sakura.css\" />\n";
    html += kCommonStyles;
    html += "</head>\n<body>\n    <h1>" + pageTitle + "</h1>\n";
    
    if (isFamily) {
        html += R"(    <form id="createBudgetForm">
        <label>Категория
            <select name="id_category" id="categorySelect" required>
                <option value="">Загрузка категорий...</option>
            </select>
            <small id="categoriesError" style="color: red; display: none;">Сначала создайте хотя бы одну категорию</small>
        </label>
        <label>Месяц
            <select name="month" required>
                <option value="1">Январь</option>
                <option value="2">Февраль</option>
                <option value="3">Март</option>
                <option value="4">Апрель</option>
                <option value="5">Май</option>
                <option value="6">Июнь</option>
                <option value="7">Июль</option>
                <option value="8">Август</option>
                <option value="9">Сентябрь</option>
                <option value="10">Октябрь</option>
                <option value="11">Ноябрь</option>
                <option value="12">Декабрь</option>
            </select>
        </label>
        <label>Год
            <input type="number" name="year" min="2000" max="2100" value="2024" required />
        </label>
        <label>Лимит
            <input type="text" name="limit_amount" placeholder="0.00" required />
        </label>
        <button type="submit" id="submitBtn">Создать бюджет</button>
        <button type="button" id="cancelBudgetEdit" style="display:none; margin-left:8px;">Отмена</button>
    </form>
)";
    } else {
        html += R"(    <form id="createBudgetForm">
        <label>Категория
            <select name="id_category" id="categorySelect" required>
                <option value="">Загрузка категорий...</option>
            </select>
            <small id="categoriesError" style="color: red; display: none;">Сначала создайте хотя бы одну категорию</small>
        </label>
        <label>Месяц
            <select name="month" required>
                <option value="1">Январь</option>
                <option value="2">Февраль</option>
                <option value="3">Март</option>
                <option value="4">Апрель</option>
                <option value="5">Май</option>
                <option value="6">Июнь</option>
                <option value="7">Июль</option>
                <option value="8">Август</option>
                <option value="9">Сентябрь</option>
                <option value="10">Октябрь</option>
                <option value="11">Ноябрь</option>
                <option value="12">Декабрь</option>
            </select>
        </label>
        <label>Год
            <input type="number" name="year" min="2000" max="2100" value="2024" required />
        </label>
        <label>Лимит
            <input type="text" name="limit_amount" placeholder="0.00" required />
        </label>
        <label id="isFamilyLabel" style="display: none;">
            <input type="checkbox" name="is_family" id="isFamilyCheckbox" />
            Семейный бюджет
        </label>
        <button type="submit" id="submitBtn">Создать бюджет</button>
        <button type="button" id="cancelBudgetEdit" style="display:none; margin-left:8px;">Отмена</button>
    </form>
)";
    }
    
    html += R"HTML(    <p><a href="/home">← Вернуться на главную</a></p>

    <h2>Список бюджетов</h2>
    <div id="budgetsContainer">
        <p id="loadingMessage">Загрузка...</p>
        <div id="emptyMessage" style="display: none;">
            <p style="color: #666; font-style: italic;">Пока не было добавлено ни одного бюджета</p>
        </div>
        <table id="budgetsTable" style="display: none; width: 100%; border-collapse: collapse; margin-top: 1em;">
            <thead>
                <tr style="background-color: #f0f0f0;">
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Категория</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Месяц</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Год</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Лимит</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Тип доступа</th>
                    <th style="padding: 0.5em; text-align: left; border-bottom: 2px solid #ddd;">Действия</th>
                </tr>
            </thead>
            <tbody id="budgetsTableBody">
            </tbody>
        </table>
    </div>

    <div id="editBudgetModal" style="display:none; position:fixed; z-index:9999; left:0; top:0; width:100%; height:100%; overflow:auto; background-color: rgba(0,0,0,0.4);">
        <div style="background:#fff; margin:8% auto; padding:20px; border:1px solid #888; width:90%; max-width:520px; border-radius:6px;">
            <h3>Редактировать бюджет</h3>
            <form id="editBudgetForm">
                <label>Категория
                    <select id="editBudgetCategory" required></select>
                </label>
                <label>Месяц
                    <select id="editBudgetMonth" required>
                        <option value="1">Январь</option>
                        <option value="2">Февраль</option>
                        <option value="3">Март</option>
                        <option value="4">Апрель</option>
                        <option value="5">Май</option>
                        <option value="6">Июнь</option>
                        <option value="7">Июль</option>
                        <option value="8">Август</option>
                        <option value="9">Сентябрь</option>
                        <option value="10">Октябрь</option>
                        <option value="11">Ноябрь</option>
                        <option value="12">Декабрь</option>
                    </select>
                </label>
                <label>Год
                    <input type="number" id="editBudgetYear" min="2000" max="2100" required />
                </label>
                <label>Лимит
                    <input type="text" id="editBudgetLimit" required />
                </label>
                <div style="margin-top:12px;">
                    <button type="submit">Сохранить</button>
                    <button type="button" onclick="closeEditBudgetModal()" style="margin-left:8px;">Отмена</button>
                </div>
            </form>
        </div>
    </div>

    <script>
        const token = localStorage.getItem("authToken") || localStorage.getItem("token");
        if (!token) {
            window.location.href = "/auth/login";
        }
        const urlParams = new URLSearchParams(window.location.search);
        const isFamilyView = urlParams.get("family") === "true";

        const monthNames = ["Январь", "Февраль", "Март", "Апрель", "Май", "Июнь",
                           "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"];

        let categoriesCache = {};
        let budgetsData = [];
        let editingBudgetId = null;
        const editBudgetModal = document.getElementById("editBudgetModal");
        const editBudgetCategory = document.getElementById("editBudgetCategory");
        const editBudgetMonth = document.getElementById("editBudgetMonth");
        const editBudgetYear = document.getElementById("editBudgetYear");
        const editBudgetLimit = document.getElementById("editBudgetLimit");

        async function loadCategoriesForDisplay() {
            try {
                const resp = await fetch("/categories" + (isFamilyView ? "?family=true" : ""), {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (resp.ok) {
                    const categories = await resp.json();
                    categoriesCache = {};
                    categories.forEach(cat => {
                        categoriesCache[cat.id] = cat;
                    });
                }
            } catch {}
        }

        async function loadBudgets() {
            const loadingMsg = document.getElementById("loadingMessage");
            const emptyMsg = document.getElementById("emptyMessage");
            const table = document.getElementById("budgetsTable");
            const tbody = document.getElementById("budgetsTableBody");
            
            try {
                const url = "/budgets" + (isFamilyView ? "?family=true" : "");
                const resp = await fetch(url, {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (!resp.ok) {
                    loadingMsg.textContent = "Ошибка загрузки бюджетов";
                    return;
                }
                const data = await resp.json();
                budgetsData = data;
                loadingMsg.style.display = "none";
                
                if (!data || data.length === 0) {
                    emptyMsg.style.display = "block";
                    table.style.display = "none";
                } else {
                    emptyMsg.style.display = "none";
                    table.style.display = "table";
                    tbody.innerHTML = "";
                    data.forEach(budget => {
                        const row = document.createElement("tr");
                        row.style.borderBottom = "1px solid #eee";
                    const category = categoriesCache[budget.id_category];
                    const categoryName = category ? category.name : "Категория недоступна";
                        const monthName = monthNames[budget.month - 1] || budget.month;
                        const accessType = budget.is_family ? "Семейный" : "Личный";
                        row.innerHTML =
                            "<td style=\"padding: 0.5em; word-break: break-word; white-space: normal;\">" + escapeHtml(categoryName) + "</td>" +
                            "<td style=\"padding: 0.5em;\">" + monthName + "</td>" +
                            "<td style=\"padding: 0.5em;\">" + budget.year + "</td>" +
                            "<td style=\"padding: 0.5em; font-weight: bold;\">" + escapeHtml(budget.limit_amount) + " руб.</td>" +
                            "<td style=\"padding: 0.5em;\">" + accessType + "</td>" +
                            "<td style=\"padding: 0.5em; display:flex; gap:6px; flex-wrap:wrap;\">" +
                                "<button onclick=\"startEditBudget(" + budget.id + ")\" style=\"padding:4px 8px;\">Редактировать</button>" +
                                "<button onclick=\"deleteBudget(" + budget.id + ")\" style=\"padding:4px 8px; background:#dc3545; color:#fff; border:none;\">Удалить</button>" +
                            "</td>";
                        tbody.appendChild(row);
                    });
                }
            } catch (error) {
                loadingMsg.textContent = "Ошибка сети: " + error.message;
                console.error("Error loading budgets:", error);
            }
        }

        function escapeHtml(text) {
            const div = document.createElement("div");
            div.textContent = text;
            return div.innerHTML;
        }

        async function loadCategories() {
            const select = document.getElementById("categorySelect");
            const errorMsg = document.getElementById("categoriesError");
            const submitBtn = document.getElementById("submitBtn");
            
            try {
                const resp = await fetch("/categories" + (isFamilyView ? "?family=true" : ""), {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (!resp.ok) {
                    select.innerHTML = "<option value=\"\">Ошибка загрузки категорий</option>";
                    return;
                }
                const categories = await resp.json();
                select.innerHTML = "";
                
                if (!categories || categories.length === 0) {
                    select.innerHTML = "<option value=\"\">Нет доступных категорий</option>";
                    select.disabled = true;
                    errorMsg.style.display = "block";
                    submitBtn.disabled = true;
                } else {
                    errorMsg.style.display = "none";
                    submitBtn.disabled = false;
                    select.disabled = false;
                    select.innerHTML = "<option value=\"\">Выберите категорию</option>";
                    categories.forEach(cat => {
                        const option = document.createElement("option");
                        option.value = cat.id;
                        const typeText = cat.type === "income" ? "Доход" : "Расход";
                        option.textContent = cat.name + " (" + typeText + ")";
                        select.appendChild(option);
                    });
                }
                if (editBudgetCategory) {
                    editBudgetCategory.innerHTML = select.innerHTML;
                }
            } catch (error) {
                select.innerHTML = "<option value=\"\">Ошибка сети</option>";
                console.error("Error loading categories:", error);
            }
        }

        const form = document.getElementById("createBudgetForm");
        if (form) {
            form.addEventListener("submit", async (e) => {
                e.preventDefault();
                const categoryId = form.id_category.value;
                if (!categoryId) {
                    alert("Пожалуйста, выберите категорию");
                    return;
                }
                const limitValue = parseFloat(form.limit_amount.value);
                if (isNaN(limitValue) || limitValue < 0) {
                    alert("Пожалуйста, введите корректный лимит (больше или равно 0)");
                    return;
                }
                
                const body = {
                    id_category: Number(categoryId),
                    month: Number(form.month.value),
                    year: Number(form.year.value),
                    limit_amount: limitValue.toString()
                };
                try {
                    const url = editingBudgetId
                        ? ("/budgets/" + editingBudgetId + (isFamilyView ? "?family=true" : ""))
                        : ("/budgets" + (isFamilyView ? "?family=true" : ""));
                    const resp = await fetch(url, {
                        method: editingBudgetId ? "PUT" : "POST",
                        headers: {
                            "Content-Type": "application/json",
                            "Authorization": "Bearer " + token
                        },
                        body: JSON.stringify(body)
                    });
                    const text = await resp.text();
                    if (resp.ok) {
                        alert(editingBudgetId ? "Бюджет обновлён" : "Бюджет создан");
                        form.reset();
                        form.year.value = new Date().getFullYear();
                        editingBudgetId = null;
                        document.getElementById("submitBtn").textContent = "Создать бюджет";
                        document.getElementById("cancelBudgetEdit").style.display = "none";
                        loadCategories();
                        loadCategoriesForDisplay().then(() => loadBudgets());
                    } else {
                        alert("Ошибка: " + text);
                    }
                } catch (error) {
                    alert("Ошибка сети: " + error.message);
                }
            });
        }

        const cancelBtnBudget = document.getElementById("cancelBudgetEdit");
        if (cancelBtnBudget) {
            cancelBtnBudget.addEventListener("click", cancelBudgetEdit);
        }

        function startEditBudget(id) {
            const budget = budgetsData.find(b => b.id === id);
            if (!budget) return;
            editingBudgetId = id;
            editBudgetCategory.value = budget.id_category;
            editBudgetMonth.value = budget.month;
            editBudgetYear.value = budget.year;
            editBudgetLimit.value = budget.limit_amount;
            editBudgetModal.style.display = "block";
        }

        function cancelBudgetEdit() {
            editingBudgetId = null;
            editBudgetModal.style.display = "none";
        }

        function closeEditBudgetModal() {
            editingBudgetId = null;
            editBudgetModal.style.display = "none";
        }

        document.getElementById("editBudgetForm").addEventListener("submit", async (e) => {
            e.preventDefault();
            if (!editingBudgetId) {
                closeEditBudgetModal();
                return;
            }
            const categoryId = editBudgetCategory.value;
            if (!categoryId) {
                alert("Пожалуйста, выберите категорию");
                return;
            }
            const limitValue = parseFloat(editBudgetLimit.value);
            if (isNaN(limitValue) || limitValue < 0) {
                alert("Пожалуйста, введите корректный лимит (больше или равно 0)");
                return;
            }
            const body = {
                id_category: Number(categoryId),
                month: Number(editBudgetMonth.value),
                year: Number(editBudgetYear.value),
                limit_amount: limitValue.toString()
            };
            try {
                const url = "/budgets/" + editingBudgetId + (isFamilyView ? "?family=true" : "");
                const resp = await fetch(url, {
                    method: "PUT",
                    headers: {
                        "Content-Type": "application/json",
                        "Authorization": "Bearer " + token
                    },
                    body: JSON.stringify(body)
                });
                const text = await resp.text();
                if (resp.ok) {
                    alert("Бюджет обновлён");
                    closeEditBudgetModal();
                    loadCategoriesForDisplay().then(() => loadBudgets());
                } else {
                    alert("Ошибка: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        });

        async function deleteBudget(id) {
            if (!confirm("Удалить бюджет?")) return;
            try {
                const resp = await fetch("/budgets/" + id + (isFamilyView ? "?family=true" : ""), {
                    method: "DELETE",
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                if (resp.status === 204) {
                    if (editingBudgetId === id) {
                        cancelBudgetEdit();
                    }
                    loadBudgets();
                } else {
                    const text = await resp.text();
                    alert("Ошибка удаления: " + text);
                }
            } catch (error) {
                alert("Ошибка сети: " + error.message);
            }
        }

        // Загружаем категории и бюджеты при загрузке страницы
        loadCategories();
        loadCategoriesForDisplay().then(() => loadBudgets());
    </script>
</body>
</html>
)HTML";
    
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    callback(resp);
}
