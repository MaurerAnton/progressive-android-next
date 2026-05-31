#include "progressive/matrix_api.hpp"
#include "progressive/tls_bridge.hpp"
#include <sstream>

namespace progressive {

// ==== Global State ====

static std::string g_homeserverBase;
static std::string g_accessToken;

void setHomeserverBaseUrl(const std::string& url) {
    g_homeserverBase = url;
    if (!g_homeserverBase.empty() && g_homeserverBase.back() == '/')
        g_homeserverBase.pop_back();
}

const std::string& getHomeserverBaseUrl() { return g_homeserverBase; }

void setAccessToken(const std::string& token) { g_accessToken = token; }

bool nativeApiAvailable() {
    return tlsBridgeAvailable() && !g_homeserverBase.empty();
}

// ==== Helper: make authenticated GET ====

static HttpResponse authGet(const std::string& path, int timeout = 30000) {
    std::string url = g_homeserverBase + path;
    std::unordered_map<std::string, std::string> headers;
    if (!g_accessToken.empty()) headers["Authorization"] = "Bearer " + g_accessToken;
    return httpGet(url, headers, timeout);
}

// Helper: make authenticated POST

static HttpResponse authPost(const std::string& path, const std::string& jsonBody, int timeout = 30000) {
    std::string url = g_homeserverBase + path;
    std::unordered_map<std::string, std::string> headers;
    if (!g_accessToken.empty()) headers["Authorization"] = "Bearer " + g_accessToken;
    return httpPost(url, jsonBody, headers, timeout);
}

// Helper: make authenticated PUT

static HttpResponse authPut(const std::string& path, const std::string& jsonBody, int timeout = 30000) {
    std::string url = g_homeserverBase + path;
    std::unordered_map<std::string, std::string> headers;
    if (!g_accessToken.empty()) headers["Authorization"] = "Bearer " + g_accessToken;
    return httpPut(url, jsonBody, timeout);
}

// ==== Auth API ====

std::string apiGetVersions() {
    auto resp = httpGet(g_homeserverBase + "/_matrix/client/versions", {}, 10000);
    return resp.success ? resp.body : "";
}

std::string apiGetLoginFlows() {
    auto resp = httpGet(g_homeserverBase + "/_matrix/client/r0/login", {}, 10000);
    return resp.success ? resp.body : "";
}

Credentials apiLogin(const std::string& userId, const std::string& password,
                     const std::string& deviceId) {
    // Build login JSON body
    std::ostringstream body;
    body << R"({"type":"m.login.password","identifier":{"type":"m.id.user","user":")"
         << userId << R"("},"password":")" << password << R"(")";
    if (!deviceId.empty()) body << R"(,"device_id":")" << deviceId << R"(")";
    body << "}";

    auto resp = httpPost(g_homeserverBase + "/_matrix/client/r0/login", body.str(),
        {{"Content-Type", "application/json"}}, 15000);

    if (!resp.isOk()) return {};

    return parseCredentials(resp.body);
}

std::string apiRegister(const std::string& username, const std::string& password,
                        const std::string& deviceId) {
    std::ostringstream body;
    body << R"({"username":")" << username << R"(","password":")" << password << R"(")";
    if (!deviceId.empty()) body << R"(,"device_id":")" << deviceId << R"(")";
    body << R"(,"initial_device_display_name":"Progressive Chat")";
    body << "}";

    auto resp = httpPost(g_homeserverBase + "/_matrix/client/r0/register",
        body.str(), {{"Content-Type", "application/json"}}, 15000);
    return resp.success ? resp.body : "";
}

bool apiUsernameAvailable(const std::string& username) {
    auto resp = httpGet(g_homeserverBase + "/_matrix/client/r0/register/available?username=" + username);
    if (!resp.isOk()) return false;
    // Response: {"available": true/false}
    return resp.body.find("\"available\":true") != std::string::npos ||
           resp.body.find("\"available\": true") != std::string::npos;
}

// ==== Sync API ====

SyncResponse apiSync(const std::string& filter, const std::string& since, int timeout) {
    std::string path = "/_matrix/client/r0/sync?timeout=" + std::to_string(timeout);
    if (!filter.empty()) path += "&filter=" + filter;
    if (!since.empty()) path += "&since=" + since;

    auto resp = authGet(path, timeout + 5000);
    if (!resp.isOk()) return {};

    return parseSyncResponse(resp.body);
}

// ==== Room API ====

std::string apiCreateRoom(const std::string& name, const std::string& topic,
                          bool isDirect, const std::vector<std::string>& inviteUsers) {
    std::ostringstream body;
    body << "{";
    if (!name.empty()) body << R"("name":")" << name << R"(",)";
    if (!topic.empty()) body << R"("topic":")" << topic << R"(",)";
    if (isDirect) body << R"("is_direct":true,)";
    if (!inviteUsers.empty()) {
        body << R"("invite":[)";
        for (size_t i = 0; i < inviteUsers.size(); i++) {
            if (i > 0) body << ",";
            body << R"(")" << inviteUsers[i] << R"(")";
        }
        body << "],";
    }
    body << R"("preset":"private_chat")";
    body << "}";

    auto resp = authPost("/_matrix/client/r0/createRoom", body.str());
    return resp.success ? resp.body : "";
}

std::string apiGetRoomMessages(const std::string& roomId, const std::string& from,
                               const std::string& dir, int limit) {
    std::string path = "/_matrix/client/r0/rooms/" + roomId + "/messages?dir=" + dir
                     + "&limit=" + std::to_string(limit);
    if (!from.empty()) path += "&from=" + from;

    auto resp = authGet(path);
    return resp.success ? resp.body : "";
}

std::string apiSendEvent(const std::string& roomId, const std::string& eventType,
                         const std::string& txnId, const std::string& contentJson) {
    std::string path = "/_matrix/client/r0/rooms/" + roomId
                     + "/send/" + eventType + "/" + txnId;
    auto resp = authPut(path, contentJson);
    return resp.success ? resp.body : "";
}

std::string apiJoinRoom(const std::string& roomId, const std::string& reason) {
    std::string body = reason.empty() ? "{}" : R"({"reason":")" + reason + R"("})";
    auto resp = authPost("/_matrix/client/r0/rooms/" + roomId + "/join", body);
    return resp.success ? resp.body : "";
}

std::string apiLeaveRoom(const std::string& roomId) {
    auto resp = authPost("/_matrix/client/r0/rooms/" + roomId + "/leave", "{}");
    return resp.success ? resp.body : "";
}

// ==== Profile API ====

std::string apiGetProfile(const std::string& userId) {
    auto resp = authGet("/_matrix/client/r0/profile/" + userId);
    return resp.success ? resp.body : "";
}

std::string apiGetDisplayName(const std::string& userId) {
    auto resp = authGet("/_matrix/client/r0/profile/" + userId + "/displayname");
    return resp.success ? resp.body : "";
}

std::string apiSetDisplayName(const std::string& userId, const std::string& displayName) {
    std::string body = R"({"displayname":")" + displayName + R"("})";
    auto resp = authPut("/_matrix/client/r0/profile/" + userId + "/displayname", body);
    return resp.success ? resp.body : "";
}

// ==== Media API ====

std::string apiUploadMedia(const std::string& fileName, const std::string& contentType,
                           const std::vector<uint8_t>& /*data*/) {
    // Media upload requires multipart form-data which is more complex.
    // For now, this is a stub — use Retrofit for media uploads.
    // Full implementation would build a multipart body with raw bytes.
    std::string path = "/_matrix/media/r0/upload?filename=" + fileName;
    auto resp = authPost(path, "{}"); // simplified
    return resp.success ? resp.body : "";
}

// ==== Additional API Implementations ====

std::string apiWhoAmI() {
    auto resp = authGet("/_matrix/client/r0/account/whoami");
    return resp.success ? resp.body : "";
}

bool apiLogout() {
    auto resp = authPost("/_matrix/client/r0/logout", "{}");
    return resp.isOk();
}

bool apiLogoutAll() {
    auto resp = authPost("/_matrix/client/r0/logout/all", "{}");
    return resp.isOk();
}

std::string apiGetPushRules() {
    auto resp = authGet("/_matrix/client/r0/pushrules");
    return resp.success ? resp.body : "";
}

std::string apiGetPushRule(const std::string& scope, const std::string& kind,
                           const std::string& ruleId) {
    std::string path = "/_matrix/client/r0/pushrules/" + scope + "/" + kind + "/" + ruleId;
    auto resp = authGet(path);
    return resp.success ? resp.body : "";
}

std::string apiSetPushRule(const std::string& scope, const std::string& kind,
                           const std::string& ruleId, const std::string& body) {
    std::string path = "/_matrix/client/r0/pushrules/" + scope + "/" + kind + "/" + ruleId;
    auto resp = authPut(path, body);
    return resp.success ? resp.body : "";
}

bool apiDeletePushRule(const std::string& scope, const std::string& kind,
                       const std::string& ruleId) {
    std::string path = "/_matrix/client/r0/pushrules/" + scope + "/" + kind + "/" + ruleId;
    auto resp = httpExecute({"DELETE", g_homeserverBase + path, "",
        {{"Authorization", "Bearer " + g_accessToken}}});
    return resp.isOk();
}

std::string apiCreateFilter(const std::string& userId, const std::string& filterJson) {
    auto resp = authPost("/_matrix/client/r0/user/" + userId + "/filter", filterJson);
    return resp.success ? resp.body : "";
}

std::string apiPublicRooms(const std::string& server, const std::string& searchTerm, int limit) {
    std::ostringstream body;
    body << R"({"limit":)" << limit;
    if (!server.empty()) body << R"(,"server":")" << server << R"(")";
    if (!searchTerm.empty()) body << R"(,"filter":{"generic_search_term":")" << searchTerm << R"("})";
    body << "}";
    auto resp = authPost("/_matrix/client/r0/publicRooms", body.str());
    return resp.success ? resp.body : "";
}

std::string apiSearch(const std::string& searchTerm, const std::string& roomId, int limit) {
    std::ostringstream body;
    body << R"({"search_categories":{"room_events":{"search_term":")" << searchTerm << R"(")";
    if (!roomId.empty()) body << R"(,"filter":{"rooms":[")" << roomId << R"("]})";
    body << R"(,"order_by":"recent","include_state":false)";
    body << "}}}";
    auto resp = authPost("/_matrix/client/r0/search", body.str());
    return resp.success ? resp.body : "";
}

std::string apiGetRoomMembers(const std::string& roomId) {
    auto resp = authGet("/_matrix/client/r0/rooms/" + roomId + "/members");
    return resp.success ? resp.body : "";
}

std::string apiInviteUser(const std::string& roomId, const std::string& userId,
                          const std::string& reason) {
    std::ostringstream body;
    body << R"({"user_id":")" << userId << R"(")";
    if (!reason.empty()) body << R"(,"reason":")" << reason << R"(")";
    body << "}";
    auto resp = authPost("/_matrix/client/r0/rooms/" + roomId + "/invite", body.str());
    return resp.success ? resp.body : "";
}

std::string apiKickUser(const std::string& roomId, const std::string& userId,
                        const std::string& reason) {
    std::ostringstream body;
    body << R"({"user_id":")" << userId << R"(")";
    if (!reason.empty()) body << R"(,"reason":")" << reason << R"(")";
    body << "}";
    auto resp = authPost("/_matrix/client/r0/rooms/" + roomId + "/kick", body.str());
    return resp.success ? resp.body : "";
}

std::string apiBanUser(const std::string& roomId, const std::string& userId,
                       const std::string& reason) {
    std::ostringstream body;
    body << R"({"user_id":")" << userId << R"(")";
    if (!reason.empty()) body << R"(,"reason":")" << reason << R"(")";
    body << "}";
    auto resp = authPost("/_matrix/client/r0/rooms/" + roomId + "/ban", body.str());
    return resp.success ? resp.body : "";
}

std::string apiUnbanUser(const std::string& roomId, const std::string& userId) {
    std::ostringstream body;
    body << R"({"user_id":")" << userId << R"("})";
    auto resp = authPost("/_matrix/client/r0/rooms/" + roomId + "/unban", body.str());
    return resp.success ? resp.body : "";
}

std::string apiRedactEvent(const std::string& roomId, const std::string& eventId,
                           const std::string& txnId, const std::string& reason) {
    std::string path = "/_matrix/client/r0/rooms/" + roomId + "/redact/" + eventId + "/" + txnId;
    std::string body = reason.empty() ? "{}" : R"({"reason":")" + reason + R"("})";
    auto resp = authPut(path, body);
    return resp.success ? resp.body : "";
}

} // namespace progressive
