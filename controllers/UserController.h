#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpBinder.h>
#include <drogon/orm/CoroMapper.h>
#include "models/Users.h"
#include "models/Families.h"

namespace finance {

class UserController : public drogon::HttpController<UserController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(UserController::Register, "/api/auth/register", drogon::Post);
        ADD_METHOD_TO(UserController::Login, "/api/auth/login", drogon::Post);
        ADD_METHOD_TO(UserController::GetProfile, "/api/auth/profile", drogon::Get);
        ADD_METHOD_TO(UserController::UpdateProfile, "/api/auth/profile", drogon::Put);
        ADD_METHOD_TO(UserController::DeleteAccount, "/api/auth/account", drogon::Delete);
        ADD_METHOD_TO(UserController::CreateFamily, "/api/family", drogon::Post);
        ADD_METHOD_TO(UserController::InviteToFamily, "/api/family/{id}/invite", drogon::Post);
        ADD_METHOD_TO(UserController::JoinFamily, "/api/family/join", drogon::Post);
        ADD_METHOD_TO(UserController::GetFamily, "/api/family", drogon::Get);
        ADD_METHOD_TO(UserController::GetFamilyMembers, "/api/family/{id}/members", drogon::Get);
        ADD_METHOD_TO(UserController::GetFamilyInvites, "/api/family/invites", drogon::Get);
        ADD_METHOD_TO(UserController::LeaveFamily, "/api/family/{id}/leave", drogon::Delete);
        ADD_METHOD_TO(UserController::RemoveFamilyMember, "/api/family/{id}/members/{user_id}", drogon::Delete);
        ADD_METHOD_TO(UserController::ShowCreateFamilyPage, "/family/create", drogon::Get);
        ADD_METHOD_TO(UserController::ShowFamilyMembersPage, "/family/members", drogon::Get);
        ADD_METHOD_TO(UserController::ShowInviteFamilyPage, "/family/invite", drogon::Get);
        ADD_METHOD_TO(UserController::ShowRegisterPage, "/auth/register", drogon::Get);
        ADD_METHOD_TO(UserController::ShowLoginPage, "/auth/login", drogon::Get);
        ADD_METHOD_TO(UserController::ShowLogoutPage, "/auth/logout", drogon::Get);
        ADD_METHOD_TO(UserController::ShowJoinFamily, "/join-family", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> Register(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> Login(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> GetProfile(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> UpdateProfile(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> DeleteAccount(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> CreateFamily(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> InviteToFamily(drogon::HttpRequestPtr req, int64_t id_family);
    drogon::Task<drogon::HttpResponsePtr> JoinFamily(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> GetFamily(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> GetFamilyMembers(drogon::HttpRequestPtr req, int64_t id_family);
    drogon::Task<drogon::HttpResponsePtr> GetFamilyInvites(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> LeaveFamily(drogon::HttpRequestPtr req, int64_t id_family);
    drogon::Task<drogon::HttpResponsePtr> RemoveFamilyMember(drogon::HttpRequestPtr req, int64_t id_family, int64_t user_id);

    void ShowRegisterPage(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)> &&callback);
    void ShowLoginPage(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)> &&callback);
    void ShowLogoutPage(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)> &&callback);

    void ShowJoinFamily(const drogon::HttpRequestPtr& req, 
                        std::function<void(const drogon::HttpResponsePtr&)> &&callback);
    void ShowCreateFamilyPage(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)> &&callback);
    void ShowFamilyMembersPage(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)> &&callback);
    void ShowInviteFamilyPage(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)> &&callback);
};

}


