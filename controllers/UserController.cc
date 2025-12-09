#include "UserController.h"
#include <drogon/HttpResponse.h>
#include <drogon/orm/CoroMapper.h>
#include <jsoncpp/json/json.h>
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpViewData.h>
#include "utils/PasswordUtils.h"
#include "utils/JwtUtils.h"
#include "models/FamilyMembers.h"
#include "models/FamilyInvite.h"

using namespace finance;
using namespace drogon_model::financial_manager;

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
using drogon::Task;

Task<HttpResponsePtr> UserController::Register(HttpRequestPtr req) {
    try {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid JSON");
            co_return resp;
        }

        if (!json->isMember("name") ||
            !json->isMember("email") ||
            !json->isMember("password")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required fields: name, email, password");
            co_return resp;
        }

        std::string name = (*json)["name"].asString();
        std::string email = (*json)["email"].asString();
        std::string password = (*json)["password"].asString();

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Users> mapper(db);

        // Проверяем, что такого email ещё нет
        try {
            auto existing = co_await mapper.findOne(
                drogon::orm::Criteria(Users::Cols::_email,
                                      drogon::orm::CompareOperator::EQ,
                                      email));
            (void)existing;
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k409Conflict);
            resp->setBody("User with this email already exists");
            co_return resp;
        } catch (const drogon::orm::UnexpectedRows & ) {
            // no user, ok
        }

        std::string hashed = security::hashPassword(password);

        Users user;
        user.setName(name);
        user.setEmail(email);
        user.setHashedPassword(hashed);

        auto inserted = co_await mapper.insert(user);

        Json::Value result;
        result["id"] = inserted.getValueOfId();
        result["name"] = inserted.getValueOfName();
        result["email"] = inserted.getValueOfEmail();
        result["token"] = jwt_utils::createToken(inserted.getValueOfId(), inserted.getValueOfEmail());

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k201Created);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "Register error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::Login(HttpRequestPtr req) {
    try {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid JSON");
            co_return resp;
        }

        if (!json->isMember("email") || !json->isMember("password")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required fields: email, password");
            co_return resp;
        }

        std::string email = (*json)["email"].asString();
        std::string password = (*json)["password"].asString();

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Users> mapper(db);

        Users user;
        try {
            user = co_await mapper.findOne(
                drogon::orm::Criteria(Users::Cols::_email,
                                      drogon::orm::CompareOperator::EQ,
                                      email));
        } catch (const drogon::orm::UnexpectedRows & ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Invalid credentials");
            co_return resp;
        }

        if (!security::verifyPassword(password, user.getValueOfHashedPassword())) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Invalid credentials");
            co_return resp;
        }

        Json::Value result;
        result["id"] = user.getValueOfId();
        result["name"] = user.getValueOfName();
        result["email"] = user.getValueOfEmail();
        result["token"] = jwt_utils::createToken(user.getValueOfId(), user.getValueOfEmail());

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "Login error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::GetProfile(HttpRequestPtr req) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Users> mapper(db);
        auto user = co_await mapper.findByPrimaryKey(static_cast<int32_t>(*userIdOpt));

        Json::Value profile;
        profile["id"] = user.getValueOfId();
        profile["name"] = user.getValueOfName();
        profile["email"] = user.getValueOfEmail();

        auto family = co_await db->execSqlCoro(
            R"(
            SELECT f.id, f.name, f.id_owner
            FROM families f
            JOIN family_members fm ON f.id = fm.id_family
            WHERE fm.id_user = $1
            )", user.getValueOfId()
        );

        if (!family.empty()) {
            profile["family"] = Json::Value(Json::objectValue);
            profile["family"]["id"] = (Json::Int64)family[0]["id"].as<int64_t>();
            profile["family"]["name"] = family[0]["name"].as<std::string>();
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(profile);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("User not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetProfile error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::UpdateProfile(HttpRequestPtr req) {
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

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Users> mapper(db);
        auto user = co_await mapper.findByPrimaryKey(static_cast<int32_t>(*userIdOpt));

        if (json->isMember("name")) {
            user.setName((*json)["name"].asString());
        }
        if (json->isMember("email")) {
            user.setEmail((*json)["email"].asString());
        }
        if (json->isMember("password")) {
            user.setHashedPassword(security::hashPassword((*json)["password"].asString()));
        }

        co_await mapper.update(user);

        Json::Value result;
        result["id"] = user.getValueOfId();
        result["name"] = user.getValueOfName();
        result["email"] = user.getValueOfEmail();

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("User not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "UpdateProfile error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::DeleteAccount(HttpRequestPtr req) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        drogon::orm::CoroMapper<Users> mapper(db);
        co_await mapper.deleteByPrimaryKey(static_cast<int32_t>(*userIdOpt));

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        co_return resp;
    } catch (const drogon::orm::UnexpectedRows &) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody("User not found");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "DeleteAccount error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::CreateFamily(HttpRequestPtr req) {
    try {
        auto IdUserOpt = jwt_utils::getUserIdFromRequest(req);
        if (!IdUserOpt) {
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
        if (!json->isMember("name")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required field name");
            co_return resp;
        }
        int64_t idUser = *IdUserOpt;
        auto db = drogon::app().getFastDbClient();
        //проверяем, есть ли пользователь уже в какой - то семье
        auto memberCheck = co_await db->execSqlCoro("SELECT 1 FROM family_members WHERE id_user = $1", idUser);
        if (!memberCheck.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("User already exists in a different family");
            co_return resp;
        }
        std::string name = (*json)["name"].asString();
        drogon::orm::CoroMapper<Families> mapper(db);

        Families family;
        family.setName(name);
        family.setIdOwner(idUser);
        auto inserted = co_await mapper.insert(family);

        drogon::orm::CoroMapper<FamilyMembers> membersMapper(db);
        FamilyMembers member;
        member.setIdFamily(inserted.getValueOfId());
        member.setIdUser(idUser);
        co_await membersMapper.insert(member);

        Json::Value res;
        res["id"] = inserted.getValueOfId();
        res["name"] = inserted.getValueOfName();
        res["id_owner"] = inserted.getValueOfIdOwner();

        auto resp = drogon::HttpResponse::newHttpJsonResponse(res);
        resp->setStatusCode(drogon::k201Created);
        co_return resp;
    }
    catch (const std::exception& e) {
        LOG_ERROR << "Login error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::InviteToFamily(HttpRequestPtr req, int64_t id_family) {
    try {
        auto IdUserOpt = jwt_utils::getUserIdFromRequest(req);
        if (!IdUserOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }
        auto db = drogon::app().getFastDbClient();

        // family_members хранит id_family/id_user как bigint -> используем int64
        int64_t id_family_i64 = id_family;
        int64_t id_user_i64 = *IdUserOpt;
        auto member = co_await db->execSqlCoro(
            "SELECT 1 FROM family_members WHERE id_family = $1::int8 AND id_user = $2::int8",
            id_family_i64, id_user_i64);
        if (member.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("You are not a member of this family");
            co_return resp;
        }
        auto json = req->getJsonObject();
        if (!json || !json->isMember("email")) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Invalid JSON or missing email field");
            co_return resp;
        }
        std::string email = (*json)["email"].asString();
        if (email.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Email cannot be empty");
            co_return resp;
        }
        std::string token = drogon::utils::getUuid();
        
        // Приводим к int32, чтобы совпадать с типами колонок (int4) и избежать ошибок
        // Явно приводим параметры к int4, чтобы типы плана соответствовали колонкам
        // family_invite хранит id_family/inviter_id как int4 -> приводим к int32
        int32_t id_family_int32 = static_cast<int32_t>(id_family);
        int32_t id_user_int32 = static_cast<int32_t>(*IdUserOpt);

        // Добавляем комментарий, чтобы принудить пересоздание prepared statement после смены типов
        co_await db->execSqlCoro(
            "/*invite_insert_v2*/ INSERT INTO family_invite (id_family, inviter_id, token, email, created_at) "
            "VALUES ($1::int4, $2::int4, $3, $4, NOW())",
            id_family_int32,
            id_user_int32,
            token,
            email
        );
        std::string joinUrl = "http://localhost:9000/join-family?token=" + token;
        LOG_INFO << "Invite to " << email << ": " << joinUrl;
        Json::Value res;
        res["message"] = "Invitation sent";
        res["email"] = email;
        res["join_url"] = joinUrl;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(res);
        resp->setStatusCode(drogon::k201Created);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "InviteToFamily error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error: " + std::string(e.what()));
        co_return resp;
    } catch (...) {
        LOG_ERROR << "InviteToFamily unknown error";
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::JoinFamily(HttpRequestPtr req) {
    std::string token, email, password;
    
    // Пробуем получить данные из JSON
    auto json = req->getJsonObject();
    LOG_INFO << "[JoinFamily] request received, content-type=" << req->getHeader("Content-Type");
    if (json && json->isMember("token") && json->isMember("email") && json->isMember("password")) {
        token = (*json)["token"].asString();
        email = (*json)["email"].asString();
        password = (*json)["password"].asString();
        LOG_INFO << "[JoinFamily] got JSON payload token=" << token << " email=" << email;
    } else {
        // Пробуем получить данные из form-data
        token = req->getParameter("token");
        email = req->getParameter("email");
        password = req->getParameter("password");
        LOG_INFO << "[JoinFamily] got form-data token=" << token << " email=" << email;
        
        if (token.empty() || email.empty() || password.empty()) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Missing required fields: token, email, password");
        co_return resp;
        }
    }

    auto db = drogon::app().getFastDbClient();
    LOG_INFO << "[JoinFamily] fetching invite for token=" << token;
    auto invite = co_await db->execSqlCoro("SELECT id_family, email, used_at from family_invite WHERE token = $1", token);
    if (invite.empty()) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody("Invalid token");
        co_return resp;
    }
    if (invite[0]["used_at"].isNull() == false) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody("Invatation was already used");
        co_return resp;
    }
    if (invite[0]["email"].as<std::string>() != email) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody("Email mismatch");
        co_return resp;
    }
    LOG_INFO << "[JoinFamily] fetching user by email=" << email;
    auto user = co_await db->execSqlCoro("SELECT id, hashed_password from users WHERE email = $1", email);
    if (user.empty()) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody("User with this email wasn't found");
        co_return resp;
    }
    if (!security::verifyPassword(password, user[0]["hashed_password"].as<std::string>())) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody("Invalid password");
        co_return resp;
    }
    int64_t user_id = user[0]["id"].as<int64_t>();
    LOG_INFO << "[JoinFamily] user_id=" << user_id << " invite id_family=" << invite[0]["id_family"].as<int64_t>();
    
    // Проверяем, что пользователь не состоит уже в другой семье
    auto existingMember = co_await db->execSqlCoro(
        "SELECT id_family FROM family_members WHERE id_user = $1", user_id
    );
    if (!existingMember.empty()) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody("User is already a member of another family");
        co_return resp;
    }
    
    LOG_INFO << "[JoinFamily] inserting into family_members";
    co_await db->execSqlCoro("INSERT into family_members(id_family, id_user) VALUES ($1, $2)", 
        invite[0]["id_family"].as<int64_t>(), user_id);
    LOG_INFO << "[JoinFamily] marking invite used";
    co_await db->execSqlCoro("UPDATE family_invite SET used_at = NOW() WHERE token = $1", token);
    std::string jwt = jwt_utils::createToken(user_id, email);

    // Если это form-data запрос, перенаправляем на страницу успеха
    if (!req->getJsonObject()) {
        auto resp = drogon::HttpResponse::newRedirectionResponse("/home");
        resp->addHeader("Set-Cookie", "token=" + jwt + "; Path=/; HttpOnly");
        co_return resp;
    }

    Json::Value res;
    res["token"] = jwt;
    res["message"] = "Successfully added user to a family";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(res);
    resp->setStatusCode(drogon::k200OK);
    co_return resp;
}

void UserController::ShowRegisterPage(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    drogon::HttpViewData data;
    auto resp = drogon::HttpResponse::newHttpViewResponse("auth_register.csp", data);
    callback(resp);
}

void UserController::ShowLoginPage(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    drogon::HttpViewData data;
    auto resp = drogon::HttpResponse::newHttpViewResponse("auth_login.csp", data);
    callback(resp);
}

void UserController::ShowLogoutPage(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    drogon::HttpViewData data;
    auto resp = drogon::HttpResponse::newHttpViewResponse("logout.csp", data);
    callback(resp);
}

Task<HttpResponsePtr> UserController::GetFamily(HttpRequestPtr req) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        auto family = co_await db->execSqlCoro(
            R"(
            SELECT f.id, f.name, f.id_owner, f.created_at
            FROM families f
            JOIN family_members fm ON f.id = fm.id_family
            WHERE fm.id_user = $1
            )", *userIdOpt
        );

        if (family.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k404NotFound);
            resp->setBody("You are not a member of any family");
            co_return resp;
        }

        Json::Value result;
        result["id"] = (Json::Int64)family[0]["id"].as<int64_t>();
        result["name"] = family[0]["name"].as<std::string>();
        result["id_owner"] = (Json::Int64)family[0]["id_owner"].as<int64_t>();
        result["created_at"] = family[0]["created_at"].as<std::string>();
        result["is_owner"] = (family[0]["id_owner"].as<int64_t>() == *userIdOpt);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetFamily error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::GetFamilyMembers(HttpRequestPtr req, int64_t id_family) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        
        // Проверяем, что пользователь является членом семьи
        auto memberCheck = co_await db->execSqlCoro(
            "SELECT 1 FROM family_members WHERE id_family = $1 AND id_user = $2",
            id_family, *userIdOpt
        );
        
        if (memberCheck.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("You are not a member of this family");
            co_return resp;
        }

        auto members = co_await db->execSqlCoro(
            R"(
            SELECT fm.id_user, u.name, u.email, fm.joined_at, f.id_owner
            FROM family_members fm
            JOIN users u ON fm.id_user = u.id
            JOIN families f ON fm.id_family = f.id
            WHERE fm.id_family = $1
            ORDER BY fm.joined_at
            )", id_family
        );

        Json::Value result(Json::arrayValue);
        for (const auto &row : members) {
            Json::Value member;
            member["id"] = (Json::Int64)row["id_user"].as<int64_t>();
            member["name"] = row["name"].as<std::string>();
            member["email"] = row["email"].as<std::string>();
            member["joined_at"] = row["joined_at"].as<std::string>();
            member["is_owner"] = (row["id_owner"].as<int64_t>() == row["id_user"].as<int64_t>());
            result.append(member);
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetFamilyMembers error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::LeaveFamily(HttpRequestPtr req, int64_t id_family) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        
        // Проверяем, что пользователь является членом семьи
        auto memberCheck = co_await db->execSqlCoro(
            "SELECT 1 FROM family_members WHERE id_family = $1 AND id_user = $2",
            id_family, *userIdOpt
        );
        
        if (memberCheck.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("You are not a member of this family");
            co_return resp;
        }

        // Проверяем, что пользователь не является владельцем
        auto family = co_await db->execSqlCoro(
            "SELECT id_owner FROM families WHERE id = $1", id_family
        );
        
        if (!family.empty() && family[0]["id_owner"].as<int64_t>() == *userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Owner cannot leave the family. Transfer ownership first or delete the family");
            co_return resp;
        }

        // Удаляем пользователя из семьи
        co_await db->execSqlCoro(
            "DELETE FROM family_members WHERE id_family = $1 AND id_user = $2",
            id_family, *userIdOpt
        );

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k200OK);
        resp->setBody("Successfully left the family");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "LeaveFamily error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::RemoveFamilyMember(HttpRequestPtr req, int64_t id_family, int64_t user_id) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        
        // Проверяем, что запрашивающий является владельцем семьи
        auto family = co_await db->execSqlCoro(
            "SELECT id_owner FROM families WHERE id = $1", id_family
        );
        
        if (family.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k404NotFound);
            resp->setBody("Family not found");
            co_return resp;
        }

        if (family[0]["id_owner"].as<int64_t>() != *userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k403Forbidden);
            resp->setBody("Only the family owner can remove members");
            co_return resp;
        }

        // Нельзя удалить владельца
        if (user_id == family[0]["id_owner"].as<int64_t>()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("Cannot remove the family owner");
            co_return resp;
        }

        // Удаляем пользователя из семьи
        auto result = co_await db->execSqlCoro(
            "DELETE FROM family_members WHERE id_family = $1 AND id_user = $2",
            id_family, user_id
        );

        if (result.affectedRows() == 0) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k404NotFound);
            resp->setBody("User is not a member of this family");
            co_return resp;
        }

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k200OK);
        resp->setBody("Member successfully removed from family");
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "RemoveFamilyMember error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

Task<HttpResponsePtr> UserController::GetFamilyInvites(HttpRequestPtr req) {
    try {
        auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
        if (!userIdOpt) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->setBody("Unauthorized");
            co_return resp;
        }

        auto db = drogon::app().getFastDbClient();
        
        // Получаем email пользователя
        auto user = co_await db->execSqlCoro(
            "SELECT email FROM users WHERE id = $1", *userIdOpt
        );
        
        if (user.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k404NotFound);
            resp->setBody("User not found");
            co_return resp;
        }
        
        std::string email = user[0]["email"].as<std::string>();
        
        // Получаем приглашения для этого email
        auto invites = co_await db->execSqlCoro(
            R"(
            SELECT fi.id, fi.id_family, fi.email, fi.created_at, fi.token,
                   f.name as family_name, u.name as inviter_name
            FROM family_invite fi
            JOIN families f ON fi.id_family = f.id
            JOIN users u ON fi.inviter_id = u.id
            WHERE fi.email = $1
            AND fi.used_at IS NULL
            ORDER BY fi.created_at DESC
            )", email
        );

        Json::Value result(Json::arrayValue);
        for (const auto &row : invites) {
            Json::Value invite;
            invite["id"] = (Json::Int64)row["id"].as<int64_t>();
            invite["id_family"] = (Json::Int64)row["id_family"].as<int64_t>();
            invite["family_name"] = row["family_name"].as<std::string>();
            invite["inviter_name"] = row["inviter_name"].as<std::string>();
            invite["email"] = row["email"].as<std::string>();
            invite["token"] = row["token"].as<std::string>();
            invite["created_at"] = row["created_at"].as<std::string>();
            invite["join_url"] = "http://localhost:9000/join-family?token=" + row["token"].as<std::string>();
            result.append(invite);
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k200OK);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "GetFamilyInvites error: " << e.what();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Internal server error");
        co_return resp;
    }
}

void UserController::ShowJoinFamily(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    auto token = req->getParameter("token");
    if (token.empty()) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody("Invalid invitation token");
        callback(resp);
        return;
    }

    auto db = drogon::app().getFastDbClient();
    db->execSqlAsync(
        "SELECT id_family, email, used_at FROM family_invite WHERE token = $1",
        [token, callback](const drogon::orm::Result &invite) {
            bool hasError = false;
            std::string errorMessage;
            std::string email;

            if (invite.empty()) {
                hasError = true;
                errorMessage = "Invalid invitation token";
            } else if (!invite[0]["used_at"].isNull()) {
                hasError = true;
                errorMessage = "This invitation has already been used";
            } else {
                email = invite[0]["email"].as<std::string>();
            }

            std::string html = R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Присоединиться к семейному аккаунту</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 500px;
            margin: 50px auto;
            padding: 20px;
        }
        h1 { color: #333; }
        .error {
            color: red;
            background-color: #ffe6e6;
            padding: 10px;
            border-radius: 5px;
            margin-bottom: 20px;
        }
        form { display: flex; flex-direction: column; gap: 15px; }
        input {
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 5px;
            font-size: 16px;
        }
        button {
            padding: 10px 20px;
            background-color: #4CAF50;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
        }
        button:hover { background-color: #45a049; }
        p { margin-top: 20px; }
        a { color: #4CAF50; text-decoration: none; }
        a:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <h1>Присоединиться к семейному аккаунту</h1>
)";

            if (hasError) {
                html += "    <div class=\"error\">" + errorMessage + "</div>\n";
            }

            html += R"(
    <form method="POST" action="/api/family/join">
        <input type="hidden" name="token" value=")" + token + R"(">
        <input type="email" name="email" placeholder="Ваш email" value=")" + email + R"(" required>
        <input type="password" name="password" placeholder="Пароль" required>
        <button type="submit">Войти и присоединиться</button>
    </form>

    <p>Ещё не зарегистрированы? 
        <a href="/auth/register?token=)" + token + R"(">Создайте аккаунт</a>
    </p>
</body>
</html>
)";

            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody(html);
            resp->setContentTypeCode(drogon::CT_TEXT_HTML);
            callback(resp);
        },
        [callback](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "ShowJoinFamily async error: " << e.base().what();
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k500InternalServerError);
            resp->setBody("Error validating invitation");
            callback(resp);
        },
        token
    );
}

void UserController::ShowCreateFamilyPage(const drogon::HttpRequestPtr& req,
                                         std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    std::string errorMessage = "";
    bool hasError = false;
    
    // Проверяем, не состоит ли уже пользователь в семье
    auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
    if (userIdOpt) {
        auto db = drogon::app().getFastDbClient();
        auto family = db->execSqlSync(
            "SELECT 1 FROM family_members WHERE id_user = $1", *userIdOpt
        );
        if (!family.empty()) {
            hasError = true;
            errorMessage = "Вы уже состоите в семье. Пожалуйста, покиньте текущую семью перед созданием новой.";
        }
    }
    
    std::string html = R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Создать семейный аккаунт</title>
    <link rel="stylesheet" href="https://unpkg.com/sakura.css/css/sakura.css" />
    <style>
        .error {
            color: red;
            background-color: #ffe6e6;
            padding: 10px;
            border-radius: 5px;
            margin-bottom: 20px;
        }
        form {
            max-width: 500px;
            margin: 20px 0;
        }
        input {
            width: 100%;
            padding: 10px;
            margin: 10px 0;
            border: 1px solid #ddd;
            border-radius: 5px;
        }
        button {
            padding: 10px 20px;
            background-color: #4CAF50;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
        }
        button:hover {
            background-color: #45a049;
        }
    </style>
</head>
<body>
    <h1>Создать семейный аккаунт</h1>
)";
    
    if (hasError) {
        html += "    <div class=\"error\">" + errorMessage + "</div>\n";
    }
    
    html += R"(    <form id="createFamilyForm">
        <label for="name">Название семьи:</label>
        <input type="text" id="name" name="name" required placeholder="Например: Семья Ивановых">
        <button type="submit">Создать семью</button>
    </form>
    
    <p><a href="/home">Вернуться на главную</a></p>
    
    <script>
        document.getElementById("createFamilyForm").addEventListener("submit", async function(e) {
            e.preventDefault();
            
            const name = document.getElementById("name").value;
            const token = localStorage.getItem("authToken") || localStorage.getItem("token") || getCookie("token");
            
            try {
                const response = await fetch("/api/family", {
                    method: "POST",
                    headers: {
                        "Content-Type": "application/json",
                        "Authorization": "Bearer " + token
                    },
                    body: JSON.stringify({ name: name })
                });
                
                if (response.ok) {
                    alert("Семья успешно создана!");
                    window.location.href = "/home";
                } else {
                    const error = await response.text();
                    alert("Ошибка: " + error);
                }
            } catch (error) {
                alert("Ошибка при создании семьи: " + error.message);
            }
        });
        
        function getCookie(name) {
            const value = "; " + document.cookie;
            const parts = value.split("; " + name + "=");
            if (parts.length === 2) return parts.pop().split(";").shift();
        }
    </script>
</body>
</html>
)";
    
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    callback(resp);
}

void UserController::ShowFamilyMembersPage(const drogon::HttpRequestPtr& req,
                                          std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    auto userIdOpt = jwt_utils::getUserIdFromRequest(req);
    if (!userIdOpt) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k401Unauthorized);
        resp->setBody("Unauthorized");
        callback(resp);
        return;
    }
    auto db = drogon::app().getFastDbClient();
    db->execSqlAsync(
        R"(
        SELECT f.id, f.name, f.id_owner
        FROM families f
        JOIN family_members fm ON f.id = fm.id_family
        WHERE fm.id_user = $1
        )",
        [callback, userIdOpt](const drogon::orm::Result &family) {
            std::string html = R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Члены семьи</title>
    <link rel="stylesheet" href="https://unpkg.com/sakura.css/css/sakura.css" />
    <style>
        .error {
            color: red;
            background-color: #ffe6e6;
            padding: 10px;
            border-radius: 5px;
            margin-bottom: 20px;
        }
        .member-list { margin: 20px 0; }
        .member-item {
            padding: 15px;
            border: 1px solid #ddd;
            border-radius: 5px;
            margin: 10px 0;
            background-color: #f9f9f9;
        }
        .owner-badge {
            background-color: #ff9800;
            color: white;
            padding: 3px 8px;
            border-radius: 3px;
            font-size: 12px;
            margin-left: 10px;
        }
    </style>
</head>
<body>
)";

            if (family.empty()) {
                html += "    <h1>Члены семьи</h1>\n";
                html += "    <div class=\"error\">Вы не состоите в семье</div>\n";
            } else {
                int64_t familyId = family[0]["id"].as<int64_t>();
                std::string familyName = family[0]["name"].as<std::string>();
                bool isOwner = (family[0]["id_owner"].as<int64_t>() == *userIdOpt);
                
                (void)isOwner; // пока не используется в HTML
                
                html += "    <h1>Члены семьи: " + familyName + "</h1>\n";
                html += "    <div id=\"membersContainer\">\n";
                html += "        <p>Загрузка списка членов...</p>\n";
                html += "    </div>\n";
                
                html += R"(
    <script>
        async function loadMembers() {
            const token = localStorage.getItem("authToken") || localStorage.getItem("token") || getCookie("token");
            const familyId = )" + std::to_string(familyId) + R"(;
            
            try {
                const response = await fetch("/api/family/" + familyId + "/members", {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                
                if (response.ok) {
                    const members = await response.json();
                    displayMembers(members);
                } else {
                    document.getElementById("membersContainer").innerHTML = 
                        "<div class=\"error\">Ошибка при загрузке списка членов</div>";
                }
            } catch (error) {
                document.getElementById("membersContainer").innerHTML = 
                    "<div class=\"error\">Ошибка: " + error.message + "</div>";
            }
        }
        
        function displayMembers(members) {
            const container = document.getElementById("membersContainer");
            
            if (members.length === 0) {
                container.innerHTML = "<p>В семье пока нет других членов.</p>";
                return;
            }
            
            let html = "<div class=\"member-list\">";
            members.forEach(member => {
                html += "<div class=\"member-item\">";
                html += "<strong>" + escapeHtml(member.name) + "</strong>";
                if (member.is_owner) {
                    html += "<span class=\"owner-badge\">Владелец</span>";
                }
                html += "<p>Email: " + escapeHtml(member.email) + "</p>";
                html += "<p>Присоединился: " + new Date(member.joined_at).toLocaleDateString("ru-RU") + "</p>";
                html += "</div>";
            });
            html += "</div>";
            
            container.innerHTML = html;
        }
        
        function escapeHtml(text) {
            const div = document.createElement("div");
            div.textContent = text;
            return div.innerHTML;
        }
        
        function getCookie(name) {
            const value = "; " + document.cookie;
            const parts = value.split("; " + name + "=");
            if (parts.length === 2) return parts.pop().split(";").shift();
        }
        
        loadMembers();
    </script>
)";
            }
            
            html += R"(
    <p><a href="/home">Вернуться на главную</a></p>
</body>
</html>
)";
            
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody(html);
            resp->setContentTypeCode(drogon::CT_TEXT_HTML);
            callback(resp);
        },
        [callback](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "ShowFamilyMembersPage error: " << e.base().what();
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k500InternalServerError);
            resp->setBody("Error loading family members");
            callback(resp);
        },
        *userIdOpt
    );
}

void UserController::ShowInviteFamilyPage(const drogon::HttpRequestPtr& req,
                                         std::function<void(const drogon::HttpResponsePtr&)> &&callback) {
    std::string html = R"HTML(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Пригласить в семью</title>
    <link rel="stylesheet" href="https://unpkg.com/sakura.css/css/sakura.css" />
    <style>
        .error {
            color: red;
            background-color: #ffe6e6;
            padding: 10px;
            border-radius: 5px;
            margin-bottom: 20px;
        }
        .loading {
            color: #666;
            font-style: italic;
        }
        form {
            max-width: 500px;
            margin: 20px 0;
        }
        input {
            width: 100%;
            padding: 10px;
            margin: 10px 0;
            border: 1px solid #ddd;
            border-radius: 5px;
        }
        button {
            padding: 10px 20px;
            background-color: #4CAF50;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
        }
        button:hover {
            background-color: #45a049;
        }
    </style>
</head>
<body>
    <h1>Пригласить в семью</h1>
    <div id="loadingMessage" class="loading">Загрузка...</div>
    <div id="errorMessage" class="error" style="display: none;"></div>
    <div id="inviteFormContainer" style="display: none;">
        <form id="inviteForm">
            <label for="email">Email приглашаемого:</label>
            <input type="email" id="email" name="email" required placeholder="example@mail.com">
            <button type="submit">Отправить приглашение</button>
        </form>
    </div>
    
    <script>
        const token = localStorage.getItem("authToken") || localStorage.getItem("token");
        if (!token) {
            document.getElementById("loadingMessage").style.display = "none";
            document.getElementById("errorMessage").textContent = "Вы не авторизованы. Пожалуйста, войдите в систему.";
            document.getElementById("errorMessage").style.display = "block";
            setTimeout(() => {
                window.location.href = "/auth/login";
            }, 2000);
        } else {
            loadFamilyInfo();
        }
        
        async function loadFamilyInfo() {
            try {
                const response = await fetch("/api/family", {
                    headers: {
                        "Authorization": "Bearer " + token
                    }
                });
                
                if (!response.ok) {
                    if (response.status === 401) {
                        document.getElementById("loadingMessage").style.display = "none";
                        document.getElementById("errorMessage").textContent = "Ошибка авторизации. Пожалуйста, войдите в систему.";
                        document.getElementById("errorMessage").style.display = "block";
                        setTimeout(() => {
                            window.location.href = "/auth/login";
                        }, 2000);
                        return;
                    }
                    const error = await response.text();
                    throw new Error(error);
                }
                
                const family = await response.json();
                if (!family || !family.id) {
                    document.getElementById("loadingMessage").style.display = "none";
                    document.getElementById("errorMessage").textContent = "Вы не состоите в семье";
                    document.getElementById("errorMessage").style.display = "block";
                    return;
                }
                
                document.getElementById("loadingMessage").style.display = "none";
                document.getElementById("inviteFormContainer").style.display = "block";
                document.querySelector("h1").textContent = "Пригласить в семью: " + family.name;
                
                document.getElementById("inviteForm").addEventListener("submit", async function(e) {
                    e.preventDefault();
                    
                    const email = document.getElementById("email").value;
                    
                    try {
                        const inviteResponse = await fetch("/api/family/" + family.id + "/invite", {
                            method: "POST",
                            headers: {
                                "Content-Type": "application/json",
                                "Authorization": "Bearer " + token
                            },
                            body: JSON.stringify({ email: email })
                        });
                        
                        if (inviteResponse.ok) {
                            const result = await inviteResponse.json();
                            alert("Приглашение успешно отправлено на " + email + "!\\nСсылка: " + result.join_url);
                            document.getElementById("email").value = "";
                        } else {
                            const error = await inviteResponse.text();
                            alert("Ошибка: " + error);
                        }
                    } catch (error) {
                        alert("Ошибка при отправке приглашения: " + error.message);
                    }
                });
            } catch (error) {
                document.getElementById("loadingMessage").style.display = "none";
                document.getElementById("errorMessage").textContent = "Ошибка: " + error.message;
                document.getElementById("errorMessage").style.display = "block";
            }
        }
    </script>
    <p><a href="/home">Вернуться на главную</a></p>
</body>
</html>
)HTML";
    
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    callback(resp);
}



