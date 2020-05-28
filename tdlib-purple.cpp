#include "config.h"
#include "td-client.h"
#include "purple-info.h"
#include "format.h"
#include <purple.h>

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <ctype.h>

static char *_(const char *s) { return const_cast<char *>(s); }

static const char *tgprpl_list_icon (PurpleAccount *acct, PurpleBuddy *buddy)
{
    return "telegram";
}

static const char *getLastOnline(const td::td_api::UserStatus &status)
{
    switch (status.get_id()) {
        case td::td_api::userStatusOnline::ID:
            return _("now");
        case td::td_api::userStatusOffline::ID: {
            const td::td_api::userStatusOffline &offline = static_cast<const td::td_api::userStatusOffline &>(status);
            time_t timestamp = offline.was_online_;
            return ctime(&timestamp);
        }
        case td::td_api::userStatusRecently::ID:
            return _("recently");
        case td::td_api::userStatusLastWeek::ID:
            return _("last week");
        case td::td_api::userStatusLastMonth::ID:
            return _("last month");
    }

    return "";
}

static PurpleTdClient *getTdClient(PurpleAccount *account)
{
    PurpleConnection *connection = purple_account_get_connection(account);
    if (connection)
        return static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(connection));
    else
        return NULL;
}

static void tgprpl_tooltip_text (PurpleBuddy *buddy, PurpleNotifyUserInfo *info, gboolean full)
{
    PurpleTdClient *tdClient = getTdClient(purple_buddy_get_account(buddy));
    if (!tdClient) return;

    std::vector<const td::td_api::user *> users;
    tdClient->getUsers(purple_buddy_get_name(buddy), users);

    if ((users.size() == 1) && users[0]->status_) {
        const char *lastOnline = getLastOnline(*users[0]->status_);
        if (lastOnline && *lastOnline)
            purple_notify_user_info_add_pair(info, _("Last online"), lastOnline);
    }
}

static GList *tgprpl_status_types (PurpleAccount *acct)
{
    GList *types = NULL;
    PurpleStatusType *type;

    type = purple_status_type_new_full (PURPLE_STATUS_AVAILABLE, NULL, NULL, FALSE, TRUE, FALSE);
    types = g_list_prepend (types, type);

    type = purple_status_type_new_full (PURPLE_STATUS_OFFLINE, NULL, NULL, FALSE, TRUE, FALSE);
    types = g_list_prepend (types, type);

    return g_list_reverse (types);
}

struct RequestData {
    PurpleAccount *account;
    std::string stringData;

    RequestData(PurpleAccount *account) : account(account) {}
};

static void cancelRequest(RequestData *data, int action)
{
    delete data;
}

static void deleteGroupConfirm(RequestData *data, int action)
{
    std::unique_ptr<RequestData> request(data);
    PurpleTdClient *tdClient = getTdClient(request->account);

    if (tdClient)
        tdClient->leaveGroup(request->stringData, true);
}

static void leaveGroupConfirm(RequestData *data, int action)
{
    std::unique_ptr<RequestData> request(data);
    PurpleTdClient *tdClient = getTdClient(request->account);

    if (tdClient)
        tdClient->leaveGroup(request->stringData, false);
}

static void leaveGroup(PurpleBlistNode *node, gpointer data)
{
    if (! PURPLE_BLIST_NODE_IS_CHAT(node))
        return;

    PurpleChat     *chat     = PURPLE_CHAT(node);
    PurpleAccount  *account  = purple_chat_get_account(chat);
    PurpleTdClient *tdClient = getTdClient(account);
    if (tdClient) {
        const char  *chatName  = getChatName(purple_chat_get_components(chat));
        RequestData *request   = new RequestData(account);
        request->stringData = chatName ? chatName : "";

        if (tdClient->getBasicGroupMembership(chatName) == BasicGroupMembership::Creator)
            purple_request_action(purple_account_get_connection(account), _("Leaving group"),
                                  _("Confirm deleting group"),
                                  _("Leaving basic group you created will delete the group. Cotinue?"),
                                  0, account, NULL, NULL, request, 2,
                                  _("_Yes"), leaveGroupConfirm, _("_No"), cancelRequest);
        else
            purple_request_action(purple_account_get_connection(account), _("Leaving group"),
                                  _("Leave the group?"), NULL,
                                  0, account, NULL, NULL, request, 2,
                                  _("_Yes"), leaveGroupConfirm, _("_No"), cancelRequest);
    }
}

static void deleteGroup(PurpleBlistNode *node, gpointer data)
{
    if (! PURPLE_BLIST_NODE_IS_CHAT(node))
        return;

    PurpleChat     *chat     = PURPLE_CHAT(node);
    PurpleAccount  *account  = purple_chat_get_account(chat);
    PurpleTdClient *tdClient = getTdClient(account);
    if (tdClient) {
        const char  *chatName  = getChatName(purple_chat_get_components(chat));

        if (tdClient->getBasicGroupMembership(chatName) == BasicGroupMembership::NonCreator)
            purple_notify_error(account, _("Error"), _("Cannot delete group"),
                                _("Cannot delete basic group created by someone else"));
        else {
            RequestData *request = new RequestData(account);
            request->stringData = chatName ? chatName : "";
            purple_request_action(purple_account_get_connection(account), _("Deleting group"),
                                  _("Delete the group?"), NULL,
                                  0, account, NULL, NULL, request, 2,
                                  _("_Yes"), deleteGroupConfirm, _("_No"), cancelRequest);
        }
    }
}

static GList* tgprpl_blist_node_menu (PurpleBlistNode *node)
{
    GList *menu = NULL;

    if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
        PurpleChat *chat = PURPLE_CHAT(node);
        if (getTdlibChatId(getChatName(purple_chat_get_components(chat))) == 0)
            return menu;

        PurpleMenuAction* action;
        action = purple_menu_action_new(_("Leave group"),
                                        PURPLE_CALLBACK(leaveGroup),
                                        NULL, NULL);
        menu = g_list_append(menu, action);
        action = purple_menu_action_new(_("Delete group"),
                                        PURPLE_CALLBACK(deleteGroup),
                                        NULL, NULL);
        menu = g_list_append(menu, action);
    }

    return menu;
}

static GList *tgprpl_chat_join_info (PurpleConnection *gc)
{
    return getChatJoinInfo();
}

static GHashTable *tgprpl_chat_info_defaults (PurpleConnection *gc, const char *chat_name)
{
    return g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
}

static ITransceiverBackend *g_testBackend = nullptr;

void tgprpl_set_test_backend(ITransceiverBackend *backend)
{
    g_testBackend = backend;
}

static void tgprpl_login (PurpleAccount *acct)
{
    PurpleConnection *gc       = purple_account_get_connection (acct);
    PurpleTdClient   *tdClient = new PurpleTdClient(acct, g_testBackend);

    purple_connection_set_protocol_data (gc, tdClient);
    getAutoDownloadLimitKb(acct);
}

static void tgprpl_close (PurpleConnection *gc)
{
    delete static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    purple_connection_set_protocol_data(gc, NULL);
}

static int tgprpl_send_im (PurpleConnection *gc, const char *who, const char *message, PurpleMessageFlags flags)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    purple_debug_misc(config::pluginId, "tgprpl_send_im to '%s' flags=0x%x\n", who, (unsigned)flags);
    return tdClient->sendMessage(who, message);
}

static unsigned int tgprpl_send_typing (PurpleConnection *gc, const char *who, PurpleTypingState typing)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    tdClient->sendTyping(who, typing == PURPLE_TYPING);
    return 0;
}

static void tgprpl_info_show (PurpleConnection *gc, const char *who)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    std::vector<const td::td_api::user *> users;
    tdClient->getUsers(who, users);

    PurpleNotifyUserInfo *info = purple_notify_user_info_new();
    if (users.empty())
        purple_notify_user_info_add_pair(info, _("User not found"), NULL);

    for (const td::td_api::user *user: users) {
        if (purple_notify_user_info_get_entries(info))
            purple_notify_user_info_add_section_break(info);

        purple_notify_user_info_add_pair(info, _("First name"), user->first_name_.c_str());
        purple_notify_user_info_add_pair(info, _("Last name"), user->last_name_.c_str());
        if (!user->username_.empty())
            purple_notify_user_info_add_pair(info, _("Username"), user->username_.c_str());
        if (!user->phone_number_.empty())
            purple_notify_user_info_add_pair(info, _("Phone number"), user->phone_number_.c_str());
        if (user->status_) {
            const char *lastOnline = getLastOnline(*user->status_);
            if (lastOnline && *lastOnline)
                purple_notify_user_info_add_pair(info, _("Last online"), lastOnline);
        }
    }

    purple_notify_userinfo(gc, who, info, NULL, NULL);
}

static void tgprpl_set_status (PurpleAccount *acct, PurpleStatus *status)
{
}

static void tgprpl_add_buddy (PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group)
{
    const char  *phoneNumber = purple_buddy_get_name(buddy);
    const char  *alias       = purple_buddy_get_alias(buddy);
    const char  *groupName   = group ? purple_group_get_name(group) : NULL;

    std::string  phoneNumberStr = phoneNumber;
    std::string  aliasStr       = alias ? alias : "";

    // Buddy will be re-added with different username if it succeeds
    purple_blist_remove_buddy(buddy);
    // Now original C strings are gone

    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    tdClient->addContact(phoneNumberStr, aliasStr, groupName ? groupName : "");
}

static void request_delete_contact_on_server_yes (RequestData *data, int action)
{
    std::unique_ptr<RequestData> request(data);
    PurpleTdClient *tdClient = getTdClient(request->account);

    if (tdClient)
        tdClient->removeContactAndPrivateChat(request->stringData);
}

static void tgprpl_request_delete_contact (PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group)
{
    g_return_if_fail(buddy);

    RequestData *data = new RequestData(purple_connection_get_account(gc));
    data->stringData = purple_buddy_get_name(buddy);

    purple_request_yes_no(gc, _("Remove contact"), _("Remove contact"),
                          _("Remove from global contact list and delete chat history from the server?\n"),
                          0, purple_connection_get_account(gc), purple_buddy_get_name(buddy),
                          NULL, data, request_delete_contact_on_server_yes,
                          cancelRequest);
}

static std::array<const char *, 3> invitePrefixes {
    "https://t.me/joinchat/",
    "https://telegram.me/joinchat/",
    "https://telegram.dog/joinchat/"
};

static bool isValidInviteLink(const char *link)
{
    return !strncmp(link, invitePrefixes[0], strlen(invitePrefixes[0])) ||
           !strncmp(link, invitePrefixes[1], strlen(invitePrefixes[1])) ||
           !strncmp(link, invitePrefixes[2], strlen(invitePrefixes[2]));
}

static void create_group_chat_cb (RequestData *data, PurpleRequestFields* fields)
{
    std::unique_ptr<RequestData> request(data);

    std::vector<std::string> members;
    for (const char *label: {"user1", "user2", "user3"}) {
        const char *userName = purple_request_fields_get_string(fields, label);
        if (userName && *userName)
            members.emplace_back(userName);
    }

    PurpleTdClient *tdClient = getTdClient(request->account);
    if (tdClient)
        tdClient->createGroup(request->stringData.c_str(), GROUP_TYPE_BASIC, members);
}

static void requestCreateBasicGroup(PurpleConnection *gc, const char *name)
{
    // Telegram doesn't allow to create chats with only one user, so we need to force
    // the user to specify at least one other one.
    PurpleRequestFields* fields = purple_request_fields_new ();
    PurpleRequestFieldGroup* group = purple_request_field_group_new (
        _("Invite at least one additional user by specifying their full name (autocompletion available)."));

    PurpleRequestField *field = purple_request_field_string_new ("user1", _("Username"), NULL, FALSE);
    purple_request_field_set_type_hint (field, "screenname");
    purple_request_field_group_add_field (group, field);

    field = purple_request_field_string_new ("user2", _("Username"), NULL, FALSE);
    purple_request_field_set_type_hint (field, "screenname");
    purple_request_field_group_add_field (group, field);

    field = purple_request_field_string_new ("user3", _("Username"), NULL, FALSE);
    purple_request_field_set_type_hint (field, "screenname");
    purple_request_field_group_add_field (group, field);

    purple_request_fields_add_group (fields, group);

    RequestData *data = new RequestData(purple_connection_get_account(gc));
    data->stringData = name;
    purple_request_fields (gc, _("Create group chat"), _("Invite users"), NULL, fields, _("OK"),
                           G_CALLBACK(create_group_chat_cb), _("Cancel"), G_CALLBACK(cancelRequest),
                           purple_connection_get_account(gc), NULL, NULL, data);
}

static void tgprpl_chat_join (PurpleConnection *gc, GHashTable *data)
{
    PurpleTdClient *tdClient   = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    const char     *name       = getChatName(data);
    const char     *inviteLink = getChatInviteLink(data);

    if (name && *name) {
        if (!tdClient->joinChat(name))
            purple_serv_got_join_chat_failed (gc, data);
    } else if (inviteLink && *inviteLink) {
        // Some user-friendliness
        if (!isValidInviteLink(inviteLink)) {
            std::string message = formatMessage(_("Invite link must begin with {}, {}, or {}"),
                                                {invitePrefixes[0], invitePrefixes[1], invitePrefixes[2]});
            purple_notify_error(gc, _("Failed to join chat"), message.c_str(), NULL);
            purple_serv_got_join_chat_failed (gc, data);
        }
        else 
            tdClient->joinChatByLink(inviteLink);
    } else {
        const char *groupName  = getChatGroupName(data);
        int         groupType  = getChatGroupType(data);
        if (groupName && *groupName && ((groupType == GROUP_TYPE_BASIC) ||
                                        (groupType == GROUP_TYPE_SUPER) ||
                                        (groupType == GROUP_TYPE_CHANNEL))) {
            if (groupType == GROUP_TYPE_BASIC)
                requestCreateBasicGroup(gc, groupName);
            else
                tdClient->createGroup(groupName, groupType, {});
        } else {
            purple_notify_error(gc, _("Failed to join chat"), _("Please enter group name and valid type"), NULL);
            purple_serv_got_join_chat_failed (gc, data);
        }
    }
}

static char *tgprpl_get_chat_name (GHashTable * data)
{
    return g_strdup(getChatName(data));
}

static void tgprpl_chat_invite (PurpleConnection *gc, int id, const char *message, const char *who)
{
}

static int tgprpl_send_chat (PurpleConnection *gc, int id, const char *message, PurpleMessageFlags flags)
{
    purple_debug_misc(config::pluginId, "Sending group chat message: purple chat id %d, flags=0x%x\n",
                      id, (unsigned)flags);
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    return tdClient->sendGroupMessage(id, message);
}

static void tgprpl_rename_buddy(PurpleConnection *gc, const char *who, const char *alias)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    tdClient->renameContact(who, alias);
}

static PurpleRoomlist *tgprpl_roomlist_get_list (PurpleConnection *gc)
{
    static PurpleRoomlist *roomlist = NULL; // put it on like protocol_data

    if (roomlist)
        purple_roomlist_unref (roomlist);
    roomlist = purple_roomlist_new (purple_connection_get_account (gc));

    purple_roomlist_set_in_progress (roomlist, TRUE);
    // blah blah blah
    purple_roomlist_set_in_progress (roomlist, FALSE);

    return roomlist;
}

static void tgprpl_roomlist_cancel (PurpleRoomlist *list)
{
}

static gboolean tgprpl_can_receive_file (PurpleConnection *gc, const char *who)
{
    return TRUE;
}

static void startUpload(PurpleXfer *xfer)
{
    PurpleAccount  *account  = static_cast<PurpleAccount *>(xfer->data);
    PurpleTdClient *tdClient = getTdClient(account);
    const char     *username = purple_xfer_get_remote_user(xfer);

    if (tdClient && username)
        tdClient->sendFileToChat(xfer, username, PURPLE_CONV_TYPE_IM);
}

static void cancelUpload(PurpleXfer *xfer)
{
    PurpleAccount  *account  = static_cast<PurpleAccount *>(xfer->data);
    PurpleTdClient *tdClient = getTdClient(account);

    if (tdClient)
        tdClient->cancelUpload(xfer);
}

static PurpleXfer *newUploadTransfer(PurpleConnection *gc, const char *who)
{
    PurpleAccount *account = purple_connection_get_account (gc);
    PurpleXfer    *xfer    = purple_xfer_new(account, PURPLE_XFER_SEND, who);
    if (xfer) {
        purple_xfer_set_init_fnc(xfer, startUpload);
        purple_xfer_set_cancel_send_fnc(xfer, cancelUpload);
        xfer->data = account;
    }

    return xfer;
}

static void tgprpl_send_file (PurpleConnection * gc, const char *who, const char *file)
{
    PurpleXfer *X = newUploadTransfer (gc, who);
    if (file) {
        purple_xfer_request_accepted (X, file);
    } else {
        purple_xfer_request (X);
    }
}

static GHashTable *tgprpl_get_account_text_table (PurpleAccount *pa)
{
    GHashTable *HT;
    HT = g_hash_table_new (g_str_hash, g_str_equal);
    static char label[] = "login_label";
    g_hash_table_insert(HT, label, _("phone no. (+ country prefix)"));
    return HT;
}

static char png[] = "png";

static PurplePluginProtocolInfo prpl_info = {
    .options          = (PurpleProtocolOptions)(OPT_PROTO_NO_PASSWORD | OPT_PROTO_IM_IMAGE),
    .user_splits      = NULL, // was initialized in tgprpl_init()
    .protocol_options = NULL, // was initialized in tgprpl_init()

    .icon_spec = {
        .format       = png,
        .min_width    = 1,
        .min_height   = 1,
        .max_width    = 512,
        .max_height   = 512,
        .max_filesize = 64000,
        .scale_rules  = PURPLE_ICON_SCALE_SEND,
    },

    .list_icon                = tgprpl_list_icon,
    .list_emblem              = NULL,
    .status_text              = NULL,
    .tooltip_text             = tgprpl_tooltip_text,
    .status_types             = tgprpl_status_types,
    .blist_node_menu          = tgprpl_blist_node_menu,
    .chat_info                = tgprpl_chat_join_info,
    .chat_info_defaults       = tgprpl_chat_info_defaults,
    .login                    = tgprpl_login,
    .close                    = tgprpl_close,
    .send_im                  = tgprpl_send_im,
    .set_info                 = NULL,
    .send_typing              = tgprpl_send_typing,
    .get_info                 = tgprpl_info_show,
    .set_status               = tgprpl_set_status,
    .set_idle                 = NULL,
    .change_passwd            = NULL,
    .add_buddy                = tgprpl_add_buddy,
    .add_buddies              = NULL,
    .remove_buddy             = tgprpl_request_delete_contact,
    .remove_buddies           = NULL,
    .add_permit               = NULL,
    .add_deny                 = NULL,
    .rem_permit               = NULL,
    .rem_deny                 = NULL,
    .set_permit_deny          = NULL,
    .join_chat                = tgprpl_chat_join,
    .reject_chat              = NULL,
    .get_chat_name            = tgprpl_get_chat_name,
    .chat_invite              = tgprpl_chat_invite,
    .chat_leave               = NULL,
    .chat_whisper             = NULL,
    .chat_send                = tgprpl_send_chat,
    .keepalive                = NULL,
    .register_user            = NULL,
    .get_cb_info              = NULL,
    .get_cb_away              = NULL,
    .alias_buddy              = tgprpl_rename_buddy,
    .group_buddy              = NULL,
    .rename_group             = NULL,
    .buddy_free               = NULL,
    .convo_closed             = NULL,
    .normalize                = NULL,
    .set_buddy_icon           = NULL,
    .remove_group             = NULL,
    .get_cb_real_name         = NULL,
    .set_chat_topic           = NULL,
    .find_blist_chat          = NULL,
    .roomlist_get_list        = tgprpl_roomlist_get_list,
    .roomlist_cancel          = tgprpl_roomlist_cancel,
    .roomlist_expand_category = NULL,
    .can_receive_file         = tgprpl_can_receive_file,
    .send_file                = tgprpl_send_file,
    .new_xfer                 = newUploadTransfer,
    .offline_message          = NULL,
    .whiteboard_prpl_ops      = NULL,
    .send_raw                 = NULL,
    .roomlist_room_serialize  = NULL,
    .unregister_user          = NULL,
    .send_attention           = NULL,
    .get_attention_types      = NULL,
    .struct_size              = sizeof(PurplePluginProtocolInfo),
    .get_account_text_table   = tgprpl_get_account_text_table,
    .initiate_media           = NULL,
    .get_media_caps           = NULL,
    .get_moods                = NULL,
    .set_public_alias         = NULL,
    .get_public_alias         = NULL,
    .add_buddy_with_invite    = NULL,
    .add_buddies_with_invite  = NULL,
};

static gboolean tgprpl_load (PurplePlugin *plugin)
{
    //purple_cmd_register("kick", "s", PURPLE_CMD_P_PLUGIN, PURPLE_CMD_FLAG_CHAT |
    //                    PURPLE_CMD_FLAG_PRPL_ONLY | PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS,
    //                    PLUGIN_ID, tgprpl_cmd_kick,
    //                    _("kick <user>:  Kick a user from the room."), NULL);

    return TRUE;
}

static void addChoice(GList *&choices, const char *description, const char *value)
{
    PurpleKeyValuePair *kvp = g_new0(PurpleKeyValuePair, 1);
    kvp->key = g_strdup(description);
    kvp->value = g_strdup(value);
    choices = g_list_append(choices, kvp);
}

static void tgprpl_init (PurplePlugin *plugin)
{
    if (purple_debug_is_verbose())
        // Log everything
        PurpleTdClient::setLogLevel(1024);
    else if (purple_debug_is_enabled())
        // Log up to info
        PurpleTdClient::setLogLevel(3);
    else
        // Log up to fatal errors and errors
        PurpleTdClient::setLogLevel(1);

    // Media and documents
    GList *choices = NULL;
    addChoice(choices, _("Ask"), AccountOptions::BigDownloadHandlingAsk);
    addChoice(choices, _("Discard"), AccountOptions::BigDownloadHandlingDiscard);

    PurpleAccountOption  *opt = purple_account_option_string_new (_("Auto-download size limit, MB (0 for unlimited)"),
                                                                  AccountOptions::AutoDownloadLimit,
                                                                  AccountOptions::AutoDownloadLimitDefault);
    prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, opt);

    opt = purple_account_option_list_new (_("Bigger file transfers"), AccountOptions::BigDownloadHandling, choices);
    prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, opt);
}

static void setTwoStepAuth(RequestData *data, PurpleRequestFields* fields);

static void requestTwoStepAuth(PurpleConnection *gc, const char *primaryText, const char *email)
{
    PurpleRequestFields     *fields  = purple_request_fields_new();
    PurpleRequestFieldGroup *group   = purple_request_field_group_new(NULL);

    PurpleRequestField *field = purple_request_field_string_new ("oldpw", _("Current password"), NULL, FALSE);
    purple_request_field_string_set_masked(field, TRUE);
    purple_request_field_group_add_field (group, field);

    field = purple_request_field_string_new ("pw1", _("New password"), NULL, FALSE);
    purple_request_field_string_set_masked(field, TRUE);
    purple_request_field_group_add_field (group, field);

    field = purple_request_field_string_new ("pw2", _("Repeat password"), NULL, FALSE);
    purple_request_field_string_set_masked(field, TRUE);
    purple_request_field_group_add_field (group, field);

    field = purple_request_field_string_new ("hint", _("Password hint"), NULL, FALSE);
    purple_request_field_group_add_field (group, field);

    field = purple_request_field_string_new ("email", _("Recovery e-mail"), email, FALSE);
    purple_request_field_group_add_field (group, field);

    purple_request_fields_add_group (fields, group);

    RequestData *data = new RequestData(purple_connection_get_account(gc));
    data->account = purple_connection_get_account(gc);
    purple_request_fields (gc, _("Two-step authentication"), primaryText, NULL, fields, _("OK"),
                           G_CALLBACK(setTwoStepAuth), _("Cancel"), G_CALLBACK(cancelRequest),
                           purple_connection_get_account(gc), NULL, NULL, data);
}

static int reRequestTwoStepAuth(gpointer user_data)
{
    std::unique_ptr<RequestData> request(static_cast<RequestData *>(user_data));
    requestTwoStepAuth(purple_account_get_connection(request->account),
                        _("Please enter same password twice"), request->stringData.c_str());
    return FALSE; // this idle handler will not be called again
}

static void setTwoStepAuth(RequestData *data, PurpleRequestFields* fields)
{
    std::unique_ptr<RequestData> request(data);
    PurpleTdClient *tdClient = getTdClient(request->account);

    if (tdClient) {
        const char *oldPass   = purple_request_fields_get_string(fields, "oldpw");
        const char *password1 = purple_request_fields_get_string(fields, "pw1");
        const char *password2 = purple_request_fields_get_string(fields, "pw2");
        const char *hint      = purple_request_fields_get_string(fields, "hint");
        const char *email     = purple_request_fields_get_string(fields, "email");

        if ((password1 != password2) && (!password1 || !password2 || strcmp(password1, password2))) {
            // Calling purple_request_fields synchronously causes glitch in pidgin
            RequestData *newRequest = new RequestData(request->account);
            if (email)
                newRequest->stringData = email;
            g_idle_add(reRequestTwoStepAuth, newRequest);
        } else if (tdClient)
            tdClient->setTwoStepAuth(oldPass, password1, hint, email);
    }
}

static void configureTwoStepAuth(PurplePluginAction *action)
{
    PurpleConnection *gc = static_cast<PurpleConnection *>(action->context);
    requestTwoStepAuth(gc, _("Enter new password and recovery e-mail address"), NULL);
}

static GList *tgprpl_actions (PurplePlugin *plugin, gpointer context)
{
    GList *actionsList = NULL;
    PurplePluginAction *action;

    action = purple_plugin_action_new(_("Configure two-step authentication..."),
                                      configureTwoStepAuth);
    actionsList = g_list_append(actionsList, action);

    return actionsList;
}

static PurplePluginInfo plugin_info = {
    .magic             = PURPLE_PLUGIN_MAGIC,
    .major_version     = PURPLE_MAJOR_VERSION,
    .minor_version     = PURPLE_MINOR_VERSION,
    .type              = PURPLE_PLUGIN_PROTOCOL,
    .ui_requirement    = NULL,
    .flags             = 0,
    .dependencies      = NULL,
    .priority          = PURPLE_PRIORITY_DEFAULT,
    .id                = config::pluginId,
    .name              = config::pluginName,
    .version           = config::versionString,
    .summary           = config::pluginSummary,
    .description       = _(config::pluginDesc),
    .author            = config::pluginAuthor,
    .homepage          = config::projectUrl,
    .load              = tgprpl_load,
    .unload            = NULL,
    .destroy           = NULL,
    .ui_info           = NULL,
    .extra_info        = &prpl_info,
    .prefs_info        = NULL,
    .actions           = tgprpl_actions,
    ._purple_reserved1 = NULL,
    ._purple_reserved2 = NULL,
    ._purple_reserved3 = NULL,
    ._purple_reserved4 = NULL,
};

extern "C" {
    PURPLE_INIT_PLUGIN (telegram_tdlib, tgprpl_init, plugin_info)
}
