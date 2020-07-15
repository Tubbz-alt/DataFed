#include <cctype>
#include <algorithm>
#include <zmq.h>
#include <unistd.h>
#include "Util.hpp"
#include "DynaLog.hpp"
#include "TraceException.hpp"
#include "DatabaseAPI.hpp"

using namespace std;

namespace SDMS {
namespace Core {

using namespace SDMS::Auth;
using namespace libjson;

#define TRANSLATE_BEGIN() try{
#define TRANSLATE_END( json ) }catch( TraceException &e ){ DL_ERROR( "INVALID JSON FROM DB: " << json.toString() ); EXCEPT_CONTEXT( e, "Invalid response from DB" ); throw; }

DatabaseAPI::DatabaseAPI( const std::string & a_db_url, const std::string & a_db_user, const std::string & a_db_pass ) :
    m_client(0), m_db_url(a_db_url)
{
    m_curl = curl_easy_init();
    if ( !m_curl )
        EXCEPT( ID_INTERNAL_ERROR, "libcurl init failed" );

    setClient("");

    curl_easy_setopt( m_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1 );
    curl_easy_setopt( m_curl, CURLOPT_USERNAME, a_db_user.c_str() );
    curl_easy_setopt( m_curl, CURLOPT_PASSWORD, a_db_pass.c_str() );
    curl_easy_setopt( m_curl, CURLOPT_WRITEFUNCTION, curlResponseWriteCB );
    curl_easy_setopt( m_curl, CURLOPT_SSL_VERIFYPEER, 0 );
    curl_easy_setopt( m_curl, CURLOPT_TCP_NODELAY, 1 );
}

DatabaseAPI::~DatabaseAPI()
{
    if ( m_client )
        curl_free( m_client );

    curl_easy_cleanup( m_curl );
}

void
DatabaseAPI::setClient( const std::string & a_client )
{
    m_client_uid = a_client.size()?(string("u/") + a_client):"";
    if ( m_client )
        curl_free( m_client );

    m_client = curl_easy_escape( m_curl, a_client.c_str(), 0 );
}

long
DatabaseAPI::dbGet( const char * a_url_path, const vector<pair<string,string>> &a_params, libjson::Value & a_result, bool a_log )
{
    a_result.clear();

    string  url;
    string  res_json;
    char    error[CURL_ERROR_SIZE];

    error[0] = 0;

    url.reserve( 512 );

    // TODO Get URL base from ctor
    url.append( m_db_url );
    url.append( a_url_path );
    url.append( "?client=" );
    url.append( m_client );

    char * esc_txt;

    for ( vector<pair<string,string>>::const_iterator iparam = a_params.begin(); iparam != a_params.end(); ++iparam )
    {
        url.append( "&" );
        url.append( iparam->first.c_str() );
        url.append( "=" );
        esc_txt = curl_easy_escape( m_curl, iparam->second.c_str(), 0 );
        url.append( esc_txt );
        curl_free( esc_txt );
    }

    if ( a_log )
    {
        DL_DEBUG( "get url: " << url );
    }

    curl_easy_setopt( m_curl, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( m_curl, CURLOPT_WRITEDATA, &res_json );
    curl_easy_setopt( m_curl, CURLOPT_ERRORBUFFER, error );
    curl_easy_setopt( m_curl, CURLOPT_HTTPGET, 1 );

    CURLcode res = curl_easy_perform( m_curl );

    long http_code = 0;
    curl_easy_getinfo( m_curl, CURLINFO_RESPONSE_CODE, &http_code );

    if ( res == CURLE_OK )
    {
        if ( res_json.size() )
        {
            try
            {
                a_result.fromString( res_json );
            }
            catch( libjson::ParseError & e )
            {
                DL_DEBUG( "PARSE [" << res_json << "]" );
                EXCEPT_PARAM( ID_SERVICE_ERROR, "Invalid JSON returned from DB: " << e.toString() );
            }
        }

        if ( http_code >= 200 && http_code < 300 )
        {
            return http_code;
        }
        else
        {
            if ( res_json.size() && a_result.asObject().has( "errorMessage" ))
            {
                EXCEPT_PARAM( ID_BAD_REQUEST, a_result.asObject().asString());
            }
            else
            {
                EXCEPT_PARAM( ID_BAD_REQUEST, "SDMS DB service call failed. Code: " << http_code << ", err: " << error );
            }
        }
    }
    else
    {
        EXCEPT_PARAM( ID_SERVICE_ERROR, "SDMS DB interface failed. error: " << error << ", " << curl_easy_strerror( res ));
    }
}


bool
DatabaseAPI::dbGetRaw( const char * a_url_path, const vector<pair<string,string>> &a_params, string & a_result )
{
    a_result.clear();

    string  url;
    char    error[CURL_ERROR_SIZE];

    a_result.clear();
    error[0] = 0;

    url.reserve( 512 );

    // TODO Get URL base from ctor
    url.append( m_db_url );
    url.append( a_url_path );
    url.append( "?client=" );
    url.append( m_client );

    char * esc_txt;

    for ( vector<pair<string,string>>::const_iterator iparam = a_params.begin(); iparam != a_params.end(); ++iparam )
    {
        url.append( "&" );
        url.append( iparam->first.c_str() );
        url.append( "=" );
        esc_txt = curl_easy_escape( m_curl, iparam->second.c_str(), 0 );
        url.append( esc_txt );
        curl_free( esc_txt );
    }

    DL_TRACE( "get raw url: " << url );

    curl_easy_setopt( m_curl, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( m_curl, CURLOPT_WRITEDATA, &a_result );
    curl_easy_setopt( m_curl, CURLOPT_ERRORBUFFER, error );
    curl_easy_setopt( m_curl, CURLOPT_HTTPGET, 1 );

    CURLcode res = curl_easy_perform( m_curl );

    long http_code = 0;
    curl_easy_getinfo( m_curl, CURLINFO_RESPONSE_CODE, &http_code );

    if ( res == CURLE_OK && ( http_code >= 200 && http_code < 300 ))
        return true;
    else
        return false;
}

long
DatabaseAPI::dbPost( const char * a_url_path, const vector<pair<string,string>> &a_params, const string * a_body, Value & a_result )
{
    //DL_DEBUG( "dbPost " << a_url_path << " [" << (a_body?*a_body:"") << "]" );

    a_result.clear();

    string  url;
    string  res_json;
    char    error[CURL_ERROR_SIZE];

    error[0] = 0;

    url.reserve( 512 );

    // TODO Get URL base from ctor
    url.append( m_db_url );
    url.append( a_url_path );
    url.append( "?client=" );
    url.append( m_client );

    char * esc_txt;

    for ( vector<pair<string,string>>::const_iterator iparam = a_params.begin(); iparam != a_params.end(); ++iparam )
    {
        url.append( "&" );
        url.append( iparam->first.c_str() );
        url.append( "=" );
        esc_txt = curl_easy_escape( m_curl, iparam->second.c_str(), 0 );
        url.append( esc_txt );
        curl_free( esc_txt );
    }

    DL_TRACE( "post url: " << url );

    curl_easy_setopt( m_curl, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( m_curl, CURLOPT_WRITEDATA, &res_json );
    curl_easy_setopt( m_curl, CURLOPT_ERRORBUFFER, error );
    curl_easy_setopt( m_curl, CURLOPT_POST, 1 );
    if ( a_body )
        curl_easy_setopt( m_curl, CURLOPT_POSTFIELDS, a_body->c_str() );

    CURLcode res = curl_easy_perform( m_curl );

    long http_code = 0;
    curl_easy_getinfo( m_curl, CURLINFO_RESPONSE_CODE, &http_code );

    if ( res == CURLE_OK )
    {
        if ( res_json.size() )
        {
            try
            {
                DL_DEBUG( "PARSE [" << res_json << "]" );
                a_result.fromString( res_json );
            }
            catch( libjson::ParseError & e )
            {
                DL_DEBUG( "PARSE [" << res_json << "]" );
                EXCEPT_PARAM( ID_SERVICE_ERROR, "Invalid JSON returned from DB: " << e.toString() );
            }
        }

        if ( http_code >= 200 && http_code < 300 )
        {
            return http_code;
        }
        else
        {
            if ( res_json.size() && a_result.asObject().has( "errorMessage" ))
            {
                EXCEPT_PARAM( ID_BAD_REQUEST, a_result.asObject().asString());
            }
            else
            {
                EXCEPT_PARAM( ID_BAD_REQUEST, "SDMS DB service call failed. Code: " << http_code << ", err: " << error );
            }
        }
    }
    else
    {
        EXCEPT_PARAM( ID_SERVICE_ERROR, "SDMS DB interface failed. error: " << error << ", " << curl_easy_strerror( res ));
    }
}

void
DatabaseAPI::clientAuthenticateByPassword( const std::string & a_password, Anon::AuthStatusReply & a_reply )
{
    Value result;

    dbGet( "usr/authn/password", {{"pw",a_password}}, result );
    setAuthStatus( a_reply, result );
}

void
DatabaseAPI::clientAuthenticateByToken( const std::string & a_token, Anon::AuthStatusReply & a_reply )
{
    Value result;

    dbGet( "usr/authn/token", {{"token",a_token}}, result );
    setAuthStatus( a_reply, result );
}

void
DatabaseAPI::setAuthStatus( Anon::AuthStatusReply & a_reply, const Value & a_result )
{
    const Value::Object & obj = a_result.asObject();
    a_reply.set_uid( obj.getString( "uid" ));
    a_reply.set_auth( obj.getBool( "authorized" ));
}

void
DatabaseAPI::clientLinkIdentity( const std::string & a_identity )
{
    Value result;

    dbGet( "usr/ident/add", {{"ident",a_identity}}, result );
}

bool
DatabaseAPI::uidByPubKey( const std::string & a_pub_key, std::string & a_uid )
{
    return dbGetRaw( "usr/find/by_pub_key", {{"pub_key",a_pub_key}}, a_uid );
}

bool
DatabaseAPI::userGetKeys( std::string & a_pub_key, std::string & a_priv_key )
{
    Value result;

    dbGet( "usr/keys/get", {}, result );

    const Value::Object & obj = result.asArray()[0].asObject();

    if ( !obj.has( "pub_key" ))
        return false;

    a_pub_key = obj.asString();

    if ( !obj.has( "priv_key" ))
        return false;

    a_priv_key = obj.asString();

    return true;
}

void
DatabaseAPI::userSetKeys( const std::string & a_pub_key, const std::string & a_priv_key )
{
    Value result;

    dbGet( "usr/keys/set", {{"pub_key",a_pub_key},{"priv_key",a_priv_key}}, result );
}

void
DatabaseAPI::userClearKeys()
{
    Value result;

    dbGet( "usr/keys/clear", {}, result );
}


void
DatabaseAPI::userGetAccessToken( std::string & a_acc_tok, std::string & a_ref_tok, uint32_t & a_expires_in )
{
    Value result;
    dbGet( "usr/token/get", {}, result );

    TRANSLATE_BEGIN()

    const Value::Object & obj = result.asObject();
    
    a_acc_tok = obj.getString( "access" );
    a_ref_tok = obj.getString( "refresh" );
    a_expires_in = (uint32_t) obj.getNumber( "expires_in" );

    TRANSLATE_END( result )
}


void
DatabaseAPI::userSetAccessToken( const std::string & a_acc_tok, uint32_t a_expires_in, const std::string & a_ref_tok )
{
    string result;
    dbGetRaw( "usr/token/set", {{"access",a_acc_tok},{"refresh",a_ref_tok},{"expires_in",to_string(a_expires_in)}}, result );
}

void
DatabaseAPI::userSetAccessToken( const Auth::UserSetAccessTokenRequest & a_request, Anon::AckReply & a_reply )
{
    (void)a_reply;
    userSetAccessToken( a_request.access(), a_request.expires_in(), a_request.refresh() );
}

void
DatabaseAPI::getExpiringAccessTokens( uint32_t a_expires_in, vector<UserTokenInfo> & a_expiring_tokens )
{
    Value result;
    dbGet( "usr/token/get/expiring", {{"expires_in",to_string(a_expires_in)}}, result );

    UserTokenInfo info;
    a_expiring_tokens.clear();

    TRANSLATE_BEGIN()

    const Value::Array & arr = result.asArray();

    a_expiring_tokens.reserve( arr.size() );

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const Value::Object & obj = i->asObject();

        info.uid = obj.getString( "id" );
        info.access_token = obj.getString( "access" );
        info.refresh_token = obj.getString( "refresh" );
        info.expiration = (uint32_t) obj.getNumber( "expiration" );

        a_expiring_tokens.push_back( info );
    }

    TRANSLATE_END( result )
}

void
DatabaseAPI::purgeTransferRecords( size_t age )
{
    string result;
    dbGetRaw( "xfr/purge", {{"age",to_string(age)}}, result );
}

void
DatabaseAPI::userCreate( const Auth::UserCreateRequest & a_request, Auth::UserDataReply & a_reply )
{
    vector<pair<string,string>> params;
    params.push_back({"uid",a_request.uid()});
    params.push_back({"password",a_request.password()});
    params.push_back({"name",a_request.name()});
    params.push_back({"email",a_request.email()});
    if ( a_request.has_options() )
        params.push_back({"options",a_request.options()});
    string uuids = "[";
    for ( int i = 0; i < a_request.uuid_size(); i++ )
    {
        if ( i )
            uuids += ",";
        uuids += "\"" + a_request.uuid(i) + "\"";
    }
    uuids += "]";
    params.push_back({"uuids",uuids});

    Value result;
    dbGet( "usr/create", params, result );

    setUserData( a_reply, result );
}


void
DatabaseAPI::userView( const UserViewRequest & a_request, UserDataReply & a_reply )
{
    vector<pair<string,string>> params;
    params.push_back({"subject",a_request.uid()});
    if ( a_request.has_details() && a_request.details() )
        params.push_back({"details","true"});

    Value result;
    dbGet( "usr/view", params, result );

    setUserData( a_reply, result );
}


void
DatabaseAPI::userUpdate( const UserUpdateRequest & a_request, UserDataReply & a_reply )
{
    Value result;

    vector<pair<string,string>> params;
    params.push_back({"subject",a_request.uid()});
    if ( a_request.has_email() )
        params.push_back({"email",a_request.email()});
    if ( a_request.has_password() )
        params.push_back({"password",a_request.password()});
    if ( a_request.has_options() )
        params.push_back({"options",a_request.options()});

    dbGet( "usr/update", params, result );

    setUserData( a_reply, result );
}


void
DatabaseAPI::userListAll( const UserListAllRequest & a_request, UserDataReply & a_reply )
{
    vector<pair<string,string>> params;
    if ( a_request.has_offset() && a_request.has_count() )
    {
        params.push_back({"offset",to_string(a_request.offset())});
        params.push_back({"count",to_string(a_request.count())});
    }

    Value result;
    dbGet( "usr/list/all", params, result );

    setUserData( a_reply, result );
}

void
DatabaseAPI::userListCollab( const UserListCollabRequest & a_request, UserDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    if ( a_request.has_offset() && a_request.has_count() )
    {
        params.push_back({"offset",to_string(a_request.offset())});
        params.push_back({"count",to_string(a_request.count())});
    }
    dbGet( "usr/list/collab", params, result );

    setUserData( a_reply, result );
}

void
DatabaseAPI::userFindByUUIDs( const Auth::UserFindByUUIDsRequest & a_request, Auth::UserDataReply & a_reply )
{
    string uuids = "[";

    for ( int i = 0; i < a_request.uuid_size(); i++ )
    {
        if ( i )
            uuids += ",";
        uuids += "\"" + a_request.uuid(i) + "\"";
    }

    uuids += "]";

    Value result;
    dbGet( "usr/find/by_uuids", {{"uuids",uuids}}, result );

    setUserData( a_reply, result );
}

void
DatabaseAPI::userFindByNameUID( const Auth::UserFindByNameUIDRequest & a_request, Auth::UserDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"name_uid",a_request.name_uid()});
    if ( a_request.has_offset() && a_request.has_count() )
    {
        params.push_back({"offset",to_string(a_request.offset())});
        params.push_back({"count",to_string(a_request.count())});
    }

    dbGet( "usr/find/by_name_uid", params, result );

    setUserData( a_reply, result );
}

void
DatabaseAPI::userGetRecentEP( const Auth::UserGetRecentEPRequest & a_request, Auth::UserGetRecentEPReply & a_reply )
{
    (void)a_request;
    Value result;

    dbGet( "usr/ep/get", {}, result );

    TRANSLATE_BEGIN()

    const Value::Array & arr = result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        a_reply.add_ep( i->asString() );
    }

    TRANSLATE_END( result )
}

void
DatabaseAPI::userSetRecentEP( const Auth::UserSetRecentEPRequest & a_request, Anon::AckReply & a_reply )
{
    (void) a_reply;
    Value result;

    string eps = "[";
    for ( int i = 0; i < a_request.ep_size(); i++ )
    {
        if ( i )
            eps += ",";
        eps += "\"" + a_request.ep(i) + "\"";
    }
    eps += "]";

    dbGet( "usr/ep/set", {{"eps",eps}}, result );
}

void
DatabaseAPI::setUserData( UserDataReply & a_reply, const Value & a_result )
{
    UserData*               user;
    Value::ArrayConstIter   k;

    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const Value::Object & obj = i->asObject();

        if ( obj.has( "paging" ))
        {
            const Value::Object & obj2 = obj.asObject();

            a_reply.set_offset( obj2.getNumber( "off" ));
            a_reply.set_count( obj2.getNumber( "cnt" ));
            a_reply.set_total( obj2.getNumber( "tot" ));
        }
        else
        {
            user = a_reply.add_user();
            user->set_uid( obj.getString( "uid" ));
            user->set_name_last( obj.getString( "name_last" ));
            user->set_name_first( obj.getString( "name_first" ));

            if ( obj.has( "email" ))
                user->set_email( obj.asString() );

            if ( obj.has( "options" ))
                user->set_options( obj.asString() );

            if ( obj.has( "is_admin" ))
                user->set_is_admin( obj.asBool() );

            if ( obj.has( "is_repo_admin" ))
                user->set_is_repo_admin( obj.asBool() );

            if ( obj.has( "idents" ))
            {
                const Value::Array & arr2 = obj.asArray();

                for ( k = arr2.begin(); k != arr2.end(); k++ )
                    user->add_ident( k->asString() );
            }

            if ( obj.has( "allocs" ))
            {
                const Value::Array & arr2 = obj.asArray();

                for ( k = arr2.begin(); k != arr2.end(); k++ )
                    setAllocData( user->add_alloc(), k->asObject() );
            }
        }
    }

    TRANSLATE_END( a_result )
}

void
DatabaseAPI::projCreate( const Auth::ProjectCreateRequest & a_request, Auth::ProjectDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;

    params.push_back({"id",a_request.id()});
    params.push_back({"title",a_request.title()});

    if ( a_request.has_desc() )
        params.push_back({"desc",a_request.desc()});

    if ( a_request.admin_size() > 0 )
    {
        string members = "[";
        for ( int i = 0; i < a_request.admin_size(); ++i )
        {
            if ( i > 0 )
                members += ",";
            members += "\"" + a_request.admin(i) + "\"";
        }
        members += "]";
        params.push_back({"admins", members });
    }

    if ( a_request.member_size() > 0 )
    {
        string members = "[";
        for ( int i = 0; i < a_request.member_size(); ++i )
        {
            if ( i > 0 )
                members += ",";
            members += "\"" + a_request.member(i) + "\"";
        }
        members += "]";
        params.push_back({"members", members });
    }

    dbGet( "prj/create", params, result );

    setProjectData( a_reply, result );
}

void
DatabaseAPI::projUpdate( const Auth::ProjectUpdateRequest & a_request, Auth::ProjectDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;

    params.push_back({"id",a_request.id()});

    if ( a_request.has_title() )
        params.push_back({"title",a_request.title()});

    if ( a_request.has_desc() )
        params.push_back({"desc",a_request.desc()});

    if ( a_request.admin_set() )
    {
        string admins = "[";
        for ( int i = 0; i < a_request.admin_size(); ++i )
        {
            if ( i > 0 )
                admins += ",";
            admins += "\"" + a_request.admin(i) + "\"";
        }
        admins += "]";
        params.push_back({ "admins", admins });
    }

    if ( a_request.member_set() )
    {
        string members = "[";
        for ( int i = 0; i < a_request.member_size(); ++i )
        {
            if ( i > 0 )
                members += ",";
            members += "\"" + a_request.member(i) + "\"";
        }
        members += "]";
        params.push_back({ "members", members });
    }

    dbGet( "prj/update", params, result );

    setProjectData( a_reply, result );
}

void
DatabaseAPI::projView( const Auth::ProjectViewRequest & a_request, Auth::ProjectDataReply & a_reply )
{
    Value result;
    dbGet( "prj/view", {{"id",a_request.id()}}, result );

    setProjectData( a_reply, result );
}

void
DatabaseAPI::projList( const Auth::ProjectListRequest & a_request, Auth::ListingReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    if ( a_request.has_subject() )
        params.push_back({"subject",a_request.subject()});
    if ( a_request.has_as_owner() && a_request.as_owner() )
        params.push_back({"as_owner","true"});
    if ( a_request.has_as_admin() && a_request.as_admin() )
        params.push_back({"as_admin","true"});
    if ( a_request.has_as_member() && a_request.as_member() )
        params.push_back({"as_member","true"});
    if ( a_request.has_sort())
        params.push_back({"sort",to_string(a_request.sort())});
    if ( a_request.has_sort_rev() && a_request.sort_rev() )
        params.push_back({"sort_rev","true"});
    if ( a_request.has_offset())
        params.push_back({"offset",to_string(a_request.offset())});
    if ( a_request.has_count())
        params.push_back({"count",to_string(a_request.count())});

    dbGet( "prj/list", params, result );

    setListingDataReply( a_reply, result );
}

void
DatabaseAPI::projGetRole( const Auth::ProjectGetRoleRequest & a_request, Auth::ProjectGetRoleReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"id",a_request.id()});
    if ( a_request.has_subject() )
        params.push_back({"subject",a_request.subject()});

    dbGet( "prj/get_role", params, result );

    const Value::Object & obj = result.asObject();
    a_reply.set_role((ProjectRole)(unsigned short) obj.getNumber( "role" ));
}

void
DatabaseAPI::projSearch( const std::string & a_query, Auth::ProjectDataReply & a_reply )
{
    Value result;

    dbGet( "prj/search", {{"query",a_query}}, result );

    setProjectData( a_reply, result );
}


void
DatabaseAPI::setProjectData( ProjectDataReply & a_reply, const Value & a_result )
{
    ProjectData*            proj;
    Value::ArrayConstIter   k;

    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const Value::Object & obj = i->asObject();

        proj = a_reply.add_proj();
        proj->set_id( obj.getString( "id" ));
        proj->set_title( obj.getString( "title" ));

        if ( obj.has( "desc" ))
            proj->set_desc( obj.asString() );

        if ( obj.has( "owner" ))
            proj->set_owner( obj.asString() );

        if ( obj.has( "ct" ))
            proj->set_ct( obj.asNumber() );

        if ( obj.has( "ut" ))
            proj->set_ut( obj.asNumber() );

        if ( obj.has( "admins" ))
        {
            const Value::Array & arr2 = obj.asArray();

            for ( k = arr2.begin(); k != arr2.end(); k++ )
                proj->add_admin( k->asString() );
        }

        if ( obj.has( "members" ))
        {
            const Value::Array & arr2 = obj.asArray();

            for ( k = arr2.begin(); k != arr2.end(); k++ )
                proj->add_member( k->asString() );
        }

        if ( obj.has( "allocs" ))
        {
            const Value::Array & arr2 = obj.asArray();

            for ( k = arr2.begin(); k != arr2.end(); k++ )
                setAllocData( proj->add_alloc(), k->asObject() );
        }
    }

    TRANSLATE_END( a_result )
}

void
DatabaseAPI::recordSearch( const RecordSearchRequest & a_request, ListingReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"query",a_request.query()});
    params.push_back({"use_client",a_request.use_client()?"true":"false"});
    params.push_back({"use_shared_users",a_request.use_shared_users()?"true":"false"});
    params.push_back({"use_shared_projects",a_request.use_shared_projects()?"true":"false"});
    if ( a_request.has_offset())
        params.push_back({"offset",to_string(a_request.offset())});
    if ( a_request.has_count() )
        params.push_back({"count",to_string(a_request.count())});

    dbGet( "/dat/search", params, result );

    setListingDataReply( a_reply, result );
}

void
DatabaseAPI::recordListByAlloc( const Auth::RecordListByAllocRequest & a_request, Auth::ListingReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"repo",a_request.repo()});
    params.push_back({"subject",a_request.subject()});
    if ( a_request.has_offset() )
        params.push_back({"offset",to_string(a_request.offset())});
    if ( a_request.has_count() )
        params.push_back({"count",to_string(a_request.count())});

    dbGet( "/dat/list/by_alloc", params, result );

    setListingDataReply( a_reply, result );
}


void
DatabaseAPI::recordView( const RecordViewRequest & a_request, RecordDataReply & a_reply )
{
    Value result;

    dbGet( "dat/view", {{"id",a_request.id()}}, result );

    setRecordData( a_reply, result );
}

void
DatabaseAPI::recordCreate( const Auth::RecordCreateRequest & a_request, Auth::RecordDataReply & a_reply )
{
    Value result;

    string body = "{\"title\":\"" + escapeJSON( a_request.title() ) + "\"";
    if ( a_request.has_desc() )
        body += ",\"desc\":\"" + escapeJSON( a_request.desc() ) + "\"";
    if ( a_request.has_keyw() )
        body += ",\"keyw\":\"" + escapeJSON( a_request.keyw() ) + "\"";
    if ( a_request.has_alias() )
        body += ",\"alias\":\"" + a_request.alias() + "\"";
    if ( a_request.has_metadata() )
        body += ",\"md\":" + a_request.metadata();
    if ( a_request.has_doi() )
        body += string(",\"doi\":\"") + a_request.doi() + "\"";
    if ( a_request.has_data_url() )
        body += string(",\"data_url\":\"") + a_request.data_url() + "\"";
    if ( a_request.has_parent_id() )
        body += ",\"parent\":\"" + a_request.parent_id() + "\"";
    if ( a_request.has_repo_id() )
        body += ",\"repo\":\"" + a_request.repo_id() + "\"";
    if ( a_request.deps_size() )
    {
        body += ",\"deps\":[";

        for ( int i = 0; i < a_request.deps_size(); i++ )
        {
            body += string(i>0?",":"")+"{\"id\":\"" + a_request.deps(i).id() + "\",\"type\":" + to_string(a_request.deps(i).type()) + "}";
        }
        body += "]";
    }
    body += "}";

    dbPost( "dat/create", {}, &body, result );

    setRecordData( a_reply, result );
}

void
DatabaseAPI::recordCreateBatch( const Auth::RecordCreateBatchRequest & a_request, Auth::RecordDataReply & a_reply )
{
    Value result;

    dbPost( "dat/create/batch", {}, &a_request.records(), result );

    setRecordData( a_reply, result );
}

void
DatabaseAPI::recordUpdate( const Auth::RecordUpdateRequest & a_request, Auth::RecordDataReply & a_reply, libjson::Value & result )
{
    string body = "{\"id\":\"" + a_request.id() + "\"";
    if ( a_request.has_title() )
        body += ",\"title\":\"" + escapeJSON( a_request.title() ) + "\"";
    if ( a_request.has_desc() )
        body += ",\"desc\":\"" + escapeJSON( a_request.desc() ) + "\"";
    if ( a_request.has_keyw() )
        body += ",\"keyw\":\"" + escapeJSON( a_request.keyw() ) + "\"";
    if ( a_request.has_alias() )
        body += ",\"alias\":\"" + a_request.alias() + "\"";
    if ( a_request.has_metadata() )
    {
        body += ",\"md\":" + (a_request.metadata().size()?a_request.metadata():"\"\"");
        if ( a_request.has_mdset() )
        {
            body += ",\"mdset\":";
            body += (a_request.mdset()?"true":"false");
        }
    }
    if ( a_request.has_doi() )
        body += string(",\"doi\":\"") + a_request.doi() + "\"";
    if ( a_request.has_data_url() )
        body += string(",\"data_url\":\"") + a_request.data_url() + "\"";
    if ( a_request.has_size() )
        body += ",\"size\":" + to_string(a_request.size());
    if ( a_request.has_source() )
        body += ",\"source\":\"" + a_request.source() + "\"";
    if ( a_request.has_ext() )
        body += ",\"ext\":\"" + a_request.ext() + "\"";
    if ( a_request.has_ext_auto() )
        body += string(",\"ext_auto\":") + (a_request.ext_auto()?"true":"false");
    if ( a_request.has_dt() )
        body += ",\"dt\":" + to_string(a_request.dt());

    if ( a_request.dep_add_size() )
    {
        body += ",\"dep_add\":[";
        for ( int i = 0; i < a_request.dep_add_size(); i++ )
        {
            body += string(i>0?",":"")+"{\"id\":\"" + a_request.dep_add(i).id() + "\",\"type\":" + to_string(a_request.dep_add(i).type()) + "}";
        }
        body += "]";
    }

    if ( a_request.dep_rem_size() )
    {
        body += ",\"dep_rem\":[";
        for ( int i = 0; i < a_request.dep_rem_size(); i++ )
        {
            body += string(i>0?",":"")+"{\"id\":\"" + a_request.dep_rem(i).id() + "\",\"type\":" + to_string(a_request.dep_rem(i).type()) + "}";
        }
        body += "]";
    }

    body += "}";

    dbPost( "dat/update", {}, &body, result );

    setRecordData( a_reply, result );
}


void
DatabaseAPI::recordUpdateBatch( const Auth::RecordUpdateBatchRequest & a_request, Auth::RecordDataReply & a_reply, libjson::Value & result )
{
    // "records" field is a JSON document - send directly to DB
    dbPost( "dat/update/batch", {}, &a_request.records(), result );

    setRecordData( a_reply, result );
}


void
DatabaseAPI::recordUpdateSize( const Auth::RepoDataSizeReply & a_size_rep )
{
    libjson::Value result;

    string body = "{\"records\":[";

    for ( int i = 0; i < a_size_rep.size_size(); i++ )
    {
        if ( i > 0 )
            body += ",";
        body += "{\"id\":\"" + a_size_rep.size(i).id() + "\",\"size\":" + to_string(a_size_rep.size(i).size()) + "}";
    }

    body += "]}";

    dbPost( "dat/update/size", {}, &body, result );
}

void
DatabaseAPI::recordExport( const Auth::RecordExportRequest & a_request, Auth::RecordExportReply & a_reply )
{
    Value result;

    string body = "{\"id\":[";

    for ( int i = 0; i < a_request.id_size(); i++ )
    {
        if ( i > 0 )
            body += ",";
        body += "\"" + a_request.id(i) + "\"";
    }

    body += "]}";

    dbPost( "dat/export", {}, &body, result );

    TRANSLATE_BEGIN()

    const Value::Array & arr = result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
        a_reply.add_record( i->asString() );

    TRANSLATE_END( result )
}

void
DatabaseAPI::recordLock( const Auth::RecordLockRequest & a_request, Auth::ListingReply & a_reply )
{
    Value result;
    string ids;

    if ( a_request.id_size() > 0 )
    {
        ids = "[";
        for ( int i = 0; i < a_request.id_size(); i++ )
        {
            if ( i > 0 )
                ids += ",";

            ids += "\"" + a_request.id(i) + "\"";
        }
        ids += "]";
    }
    else
        ids = "[]";

    dbGet( "dat/lock", {{"ids",ids},{"lock",a_request.lock()?"true":"false"}}, result );

    setListingDataReply( a_reply, result );
}

/*void
DatabaseAPI::recordGetDependencies( const Auth::RecordGetDependenciesRequest & a_request, Auth::ListingReply & a_reply )
{
    Value result;

    string ids="[";
    for ( int i = 0; i < a_request.id_size(); i++ )
    {
        if ( i > 0 )
            ids += ",";

        ids += "\"" + a_request.id(i) + "\"";
    }
    ids += "]";

    dbGet( "dat/dep/get", {{"ids", ids}}, result );

    setListingDataReply( a_reply, result );
}*/


void
DatabaseAPI::recordGetDependencyGraph( const Auth::RecordGetDependencyGraphRequest & a_request, Auth::ListingReply & a_reply )
{
    Value result;

    dbGet( "dat/dep/graph/get", {{"id",a_request.id()}}, result );

    setListingDataReply( a_reply, result );
}


void
DatabaseAPI::doiView( const Anon::DOIViewRequest & a_request, Auth::RecordDataReply & a_reply )
{
    Value result;

    dbGet( "dat/view/doi", {{"doi",a_request.doi()}}, result );

    setRecordData( a_reply, result );
}


void
DatabaseAPI::setRecordData( RecordDataReply & a_reply, const Value & a_result )
{
    RecordData *        rec;
    DependencyData *    deps;
    Value::ObjectConstIter   j, m;
    Value::ArrayConstIter    i, k;

    TRANSLATE_BEGIN()

    const Value::Object & res_obj = a_result.asObject();

    if ( res_obj.has( "results" ))
    {
        const Value::Array & arr = res_obj.asArray();

        for ( i = arr.begin(); i != arr.end(); i++ )
        {
            const Value::Object & obj = i->asObject();

            rec = a_reply.add_data();
            rec->set_id( obj.getString( "id" ));
            rec->set_title( obj.getString( "title" ));

            if ( obj.has( "alias" ) && !obj.value().isNull() )
                rec->set_alias( obj.asString() );

            if ( obj.has( "owner" ))
                rec->set_owner( obj.asString() );

            if ( obj.has( "creator" ))
                rec->set_creator( obj.asString() );

            if ( obj.has( "desc" ))
                rec->set_desc( obj.asString() );

            if ( obj.has( "keyw" ))
                rec->set_keyw( obj.asString() );

            if ( obj.has( "doi" ))
                rec->set_doi( obj.asString() );

            if ( obj.has( "data_url" ))
                rec->set_data_url( obj.asString() );

            if ( obj.has( "md" ))
                rec->set_metadata( obj.value().toString() );

            if ( obj.has( "repo_id" ))
                rec->set_repo_id( obj.asString() );

            if ( obj.has( "size" ))
                rec->set_size( obj.asNumber() );

            if ( obj.has( "source" ))
                rec->set_source( obj.asString() );

            if ( obj.has( "ext" ))
                rec->set_ext( obj.asString() );

            if ( obj.has( "ext_auto" ))
                rec->set_ext_auto( obj.asBool() );

            if ( obj.has( "ct" ))
                rec->set_ct( obj.asNumber() );

            if ( obj.has( "ut" ))
                rec->set_ut( obj.asNumber() );

            if ( obj.has( "dt" ))
                rec->set_dt( obj.asNumber() );

            if ( obj.has( "locked" ))
                rec->set_locked( obj.asBool() );

            if ( obj.has( "parent_id" ))
                rec->set_parent_id( obj.asString() );

            if ( obj.has( "notes" ))
                rec->set_notes( obj.asNumber() );

            if ( obj.has( "deps" ))
            {
                const Value::Array & arr2 = obj.asArray();

                for ( k = arr2.begin(); k != arr2.end(); k++ )
                {
                    const Value::Object & obj2 = k->asObject();

                    deps = rec->add_deps();

                    deps->set_id( obj2.getString( "id" ));
                    deps->set_type((DependencyType)(unsigned short) obj2.getNumber( "type" ));
                    deps->set_dir((DependencyDir)(unsigned short) obj2.getNumber( "dir" ));

                    if ( obj2.has( "alias" ) && !obj2.value().isNull( ))
                        deps->set_alias( obj2.asString() );
                }
            }
        }
    }

    if ( res_obj.has( "updates" ))
    {
        const Value::Array & arr = res_obj.asArray();

        for ( i = arr.begin(); i != arr.end(); i++ )
            setListingData( a_reply.add_update(), i->asObject() );
    }

    TRANSLATE_END( a_result )
}


void
DatabaseAPI::dataPath( const Auth::DataPathRequest & a_request, Auth::DataPathReply & a_reply )
{
    Value result;

    dbGet( "dat/path", {{"id",a_request.id()},{"domain",a_request.domain()}}, result );

    const Value::Object & obj = result.asObject();

    a_reply.set_path( obj.getString( "path" ));
}


void
DatabaseAPI::collList( const CollListRequest & a_request, CollDataReply & a_reply )
{
    Value result;

    if ( a_request.has_user() )
        dbGet( "col/priv/list", {{"subject",a_request.user()}}, result );
    else
        dbGet( "col/priv/list", {}, result );

    setCollData( a_reply, result );
}

void
DatabaseAPI::collListPublished( const Auth::CollListPublishedRequest & a_request, Auth::ListingReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;

    if ( a_request.has_subject() )
        params.push_back({"subject",a_request.subject()});
    if ( a_request.has_offset() )
        params.push_back({"offset",to_string(a_request.offset())});
    if ( a_request.has_count() )
        params.push_back({"count",to_string(a_request.count())});

    dbGet( "col/published/list", params, result );

    setListingDataReply( a_reply, result );
}

void
DatabaseAPI::collCreate( const Auth::CollCreateRequest & a_request, Auth::CollDataReply & a_reply )
{
    Value result;

    string body = "{\"title\":\"" + escapeJSON( a_request.title() ) + "\"";
    if ( a_request.has_desc() )
        body += ",\"desc\":\"" + escapeJSON( a_request.desc() ) + "\"";
    if ( a_request.has_alias() )
        body += ",\"alias\":\"" + a_request.alias() + "\"";
    if ( a_request.has_parent_id() )
        body += ",\"parent\":\"" + a_request.parent_id() + "\"";
    if ( a_request.has_topic() )
        body += ",\"topic\":\"" + escapeJSON( a_request.topic() ) + "\"";
    body += "}";

    dbPost( "col/create", {}, &body, result );

    setCollData( a_reply, result );
}

void
DatabaseAPI::collUpdate( const Auth::CollUpdateRequest & a_request, Auth::CollDataReply & a_reply )
{
    Value result;

    string body = "{\"id\":\"" + a_request.id() + "\"";
    if ( a_request.has_title() )
        body += ",\"title\":\"" + escapeJSON( a_request.title() ) + "\"";
    if ( a_request.has_desc() )
        body += ",\"desc\":\"" + escapeJSON( a_request.desc() ) + "\"";
    if ( a_request.has_alias() )
        body += ",\"alias\":\"" + a_request.alias() + "\"";
    if ( a_request.has_topic() )
        body += ",\"topic\":\"" + escapeJSON( a_request.topic() ) + "\"";
    body += "}";

    dbPost( "col/update", {}, &body, result );

    setCollData( a_reply, result );
}


void
DatabaseAPI::collView( const Auth::CollViewRequest & a_request, Auth::CollDataReply & a_reply )
{
    Value result;

    dbGet( "col/view", {{"id",a_request.id()}}, result );

    setCollData( a_reply, result );
}

void
DatabaseAPI::collRead( const CollReadRequest & a_request, ListingReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"id",a_request.id()});
    if ( a_request.has_offset() )
        params.push_back({"offset",to_string(a_request.offset())});
    if ( a_request.has_count() )
        params.push_back({"count",to_string(a_request.count())});

    dbGet( "col/read", params, result );

    setListingDataReply( a_reply, result );
}

void
DatabaseAPI::collWrite( const CollWriteRequest & a_request, Auth::ListingReply & a_reply )
{
    string add_list, rem_list;

    if ( a_request.add_size() > 0 )
    {
        add_list = "[";
        for ( int i = 0; i < a_request.add_size(); i++ )
        {
            if ( i > 0 )
                add_list += ",";

            add_list += "\"" + a_request.add(i) + "\"";
        }
        add_list += "]";
    }
    else
        add_list = "[]";

    if ( a_request.rem_size() > 0 )
    {
        rem_list = "[";
        for ( int i = 0; i < a_request.rem_size(); i++ )
        {
            if ( i > 0 )
                rem_list += ",";

            rem_list += "\"" + a_request.rem(i) + "\"";
        }
        rem_list += "]";
    }
    else
        rem_list = "[]";

    Value result;

    dbGet( "col/write", {{"id",a_request.id()},{"add",add_list},{"remove",rem_list}}, result );

    setListingDataReply( a_reply, result );
}

void
DatabaseAPI::collMove( const Auth::CollMoveRequest & a_request, Anon::AckReply & a_reply )
{
    (void) a_reply;

    if ( a_request.item_size() == 0 )
        return;

    string items = "[";

    for ( int i = 0; i < a_request.item_size(); i++ )
    {
        if ( i > 0 )
            items += ",";

        items += "\"" + a_request.item(i) + "\"";
    }
    items += "]";

    Value result;
    dbGet( "col/move", {{"source",a_request.src_id()},{"dest",a_request.dst_id()},{"items",items}}, result );
}

void
DatabaseAPI::collGetParents( const Auth::CollGetParentsRequest & a_request, Auth::CollPathReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"id",a_request.id()});
    if ( a_request.has_inclusive() )
        params.push_back({"inclusive",a_request.inclusive()?"true":"false"});

    dbGet( "col/get_parents", params, result );

    setCollPathData( a_reply, result );
}

void
DatabaseAPI::collGetOffset( const Auth::CollGetOffsetRequest & a_request, Auth::CollGetOffsetReply & a_reply )
{
    Value result;

    dbGet( "col/get_offset", {{"id",a_request.id()},{"item",a_request.item()},{"page_sz",to_string(a_request.page_sz())}}, result );

    a_reply.set_id( a_request.id() );
    a_reply.set_item( a_request.item() );
    a_reply.set_offset( result.asObject().getNumber( "offset" ));
}

void
DatabaseAPI::setCollData( CollDataReply & a_reply, const libjson::Value & a_result )
{
    CollData* coll;
    Value::ObjectConstIter j;

    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const  Value::Object & obj = i->asObject();

        coll = a_reply.add_coll();
        coll->set_id( obj.getString( "id" ));
        coll->set_title( obj.getString( "title" ));

        if ( obj.has( "desc" ))
            coll->set_desc( obj.asString() );

        if ( obj.has( "topic" ))
            coll->set_topic( obj.asString() );

        if ( obj.has( "alias" ) && !obj.value().isNull() )
            coll->set_alias( obj.asString() );

        if ( obj.has( "ct" ))
            coll->set_ct( obj.asNumber() );

        if ( obj.has( "ut" ))
            coll->set_ut( obj.asNumber() );

        if ( obj.has( "parent_id" ))
            coll->set_parent_id( obj.asString() );

        if ( obj.has( "owner" ))
            coll->set_owner( obj.asString() );

        if ( obj.has( "notes" ))
            coll->set_notes( obj.asNumber() );
    }

    TRANSLATE_END( a_result )
}

void
DatabaseAPI::setCollPathData( CollPathReply & a_reply, const libjson::Value & a_result )
{
    PathData *          path;
    ListingData *       item;
    Value::ArrayConstIter    j;
    Value::ObjectConstIter   k;

    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const Value::Array & arr2 = i->asArray();

        path = a_reply.add_path();

        for ( j = arr2.begin(); j != arr2.end(); j++ )
        {
            const Value::Object & obj = j->asObject();

            item = path->add_item();
            item->set_id( obj.getString( "id" ));
            item->set_title( obj.getString( "title" ));

            if ( obj.has( "alias" ) && !obj.value().isNull( ))
                item->set_alias( obj.asString() );

            if ( obj.has( "owner" ))
                item->set_owner( obj.asString() );
        }
    }

    TRANSLATE_END( a_result )
}

void
DatabaseAPI::setListingDataReply( ListingReply & a_reply, const libjson::Value & a_result )
{
    Value::ObjectConstIter   j;

    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const Value::Object & obj = i->asObject();

        if ( obj.has( "paging" ))
        {
            const Value::Object & obj2 = obj.asObject();

            a_reply.set_offset( obj2.getNumber( "off" ));
            a_reply.set_count( obj2.getNumber( "cnt" ));
            a_reply.set_total( obj2.getNumber( "tot" ));
        }
        else
        {
            setListingData( a_reply.add_item(), obj );
        }
    }

    TRANSLATE_END( a_result )
}

void
DatabaseAPI::setListingData( ListingData * a_item, const Value::Object & a_obj )
{
    if ( a_obj.has( "id" ))
        a_item->set_id( a_obj.asString() );
    else if ( a_obj.has( "_id" ))
        a_item->set_id( a_obj.asString() );

    a_item->set_title( a_obj.getString( "title" ));

    if ( a_obj.has( "alias" ) && !a_obj.value().isNull( ))
        a_item->set_alias( a_obj.asString() );

    if ( a_obj.has( "owner" ) && !a_obj.value().isNull( ))
        a_item->set_owner( a_obj.asString() );

    if ( a_obj.has( "creator" ) && !a_obj.value().isNull( ))
        a_item->set_creator( a_obj.asString() );

    if ( a_obj.has( "doi" ) && !a_obj.value().isNull( ))
        a_item->set_doi( a_obj.asString() );

    if ( a_obj.has( "url" ) && !a_obj.value().isNull( ))
        a_item->set_url( a_obj.asString() );

    if ( a_obj.has( "size" ) && !a_obj.value().isNull( ))
        a_item->set_size( a_obj.asNumber() );

    if ( a_obj.has( "notes" ))
        a_item->set_notes( a_obj.asNumber() );

    if ( a_obj.has( "locked" ) && !a_obj.value().isNull( ))
        a_item->set_locked( a_obj.asBool() );

    if ( a_obj.has( "gen" ))
        a_item->set_gen( a_obj.asNumber() );

    if ( a_obj.has( "deps" ))
    {
        const Value::Array &    arr2 = a_obj.asArray();
        DependencyData *        dep;

        a_item->set_deps_avail( true );

        for ( Value::ArrayConstIter i = arr2.begin(); i != arr2.end(); i++ )
        {
            const Value::Object & obj2 = i->asObject();

            dep = a_item->add_dep();
            dep->set_id( obj2.getString( "id" ));
            dep->set_type((DependencyType)(unsigned short) obj2.getNumber( "type" ));
            dep->set_dir((DependencyDir)(unsigned short) obj2.getNumber( "dir" ));

            if ( obj2.has( "alias" ) && !obj2.value().isNull() )
                dep->set_alias( obj2.asString() );

            if ( obj2.has( "notes" ))
                dep->set_notes( obj2.asNumber() );
        }
    }
}

void
DatabaseAPI::queryList( const Auth::QueryListRequest & a_request, Auth::ListingReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    if ( a_request.has_offset() )
        params.push_back({"offset",to_string(a_request.offset())});
    if ( a_request.has_count() )
        params.push_back({"count",to_string(a_request.count())});

    dbGet( "qry/list", params, result );

    setListingDataReply( a_reply, result );
}

void
DatabaseAPI::queryCreate( const Auth::QueryCreateRequest & a_request, Auth::QueryDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;

    params.push_back({"title",a_request.title()});
    params.push_back({"query",a_request.query()});
    params.push_back({"query_comp",a_request.query_comp()});
    if ( a_request.has_use_owner() )
        params.push_back({"use_owner",a_request.use_owner()?"true":"false"});
    if ( a_request.has_use_sh_usr() )
        params.push_back({"use_sh_usr",a_request.use_sh_usr()?"true":"false"});
    if ( a_request.has_use_sh_prj() )
        params.push_back({"use_sh_prj",a_request.use_sh_prj()?"true":"false"});

    dbGet( "qry/create", params, result );

    setQueryData( a_reply, result );
}

void
DatabaseAPI::queryUpdate( const Auth::QueryUpdateRequest & a_request, Auth::QueryDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;

    params.push_back({"id",a_request.id()});
    if ( a_request.has_title() )
        params.push_back({"title",a_request.title()});
    if ( a_request.has_query() )
        params.push_back({"query",a_request.query()});
    if ( a_request.has_query_comp() )
        params.push_back({"query_comp",a_request.query_comp()});
    if ( a_request.has_use_owner() )
        params.push_back({"use_owner",a_request.use_owner()?"true":"false"});
    if ( a_request.has_use_sh_usr() )
        params.push_back({"use_sh_usr",a_request.use_sh_usr()?"true":"false"});
    if ( a_request.has_use_sh_prj() )
        params.push_back({"use_sh_prj",a_request.use_sh_prj()?"true":"false"});

    dbGet( "qry/update", params, result );

    setQueryData( a_reply, result );
}

void
DatabaseAPI::queryDelete( const std::string & a_id )
{
    Value result;

    dbGet( "qry/delete", {{"id",a_id}}, result );
}

void
DatabaseAPI::queryView( const Auth::QueryViewRequest & a_request, Auth::QueryDataReply & a_reply )
{
    Value result;

    dbGet( "qry/view", {{"id",a_request.id()}}, result );

    setQueryData( a_reply, result );
}

void
DatabaseAPI::queryExec( const Auth::QueryExecRequest & a_request, Auth::ListingReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;

    params.push_back({"id",a_request.id()});
    if ( a_request.has_offset())
        params.push_back({"offset",to_string(a_request.offset())});
    if ( a_request.has_count() )
        params.push_back({"count",to_string(a_request.count())});

    dbGet( "/qry/exec", params, result );

    setListingDataReply( a_reply, result );
}

void
DatabaseAPI::setQueryData( QueryDataReply & a_reply, const libjson::Value & a_result )
{
    QueryData *         qry;

    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const Value::Object & obj = i->asObject();

        qry = a_reply.add_query();
        qry->set_id( obj.getString( "id" ));
        qry->set_title( obj.getString( "title" ));
        qry->set_query( obj.getString( "query" ));

        if ( obj.has( "owner" ))
            qry->set_owner( obj.asString() );

        if ( obj.has( "ct" ))
            qry->set_ct( obj.asNumber() );

        if ( obj.has( "ut" ))
            qry->set_ut( obj.asNumber() );

        if ( obj.has( "use_owner" ) && !obj.value().isNull() )
            qry->set_use_owner( obj.asBool() );

        if ( obj.has( "use_sh_usr" ) && !obj.value().isNull() )
            qry->set_use_sh_usr( obj.asBool() );

        if ( obj.has( "use_sh_prj" ) && !obj.value().isNull() )
            qry->set_use_sh_prj( obj.asBool() );
    }

    TRANSLATE_END( a_result )
}


void
DatabaseAPI::aclView( const Auth::ACLViewRequest & a_request, Auth::ACLDataReply & a_reply )
{
    libjson::Value result;

    dbGet( "acl/view", {{"id",a_request.id()}}, result );

    setACLData( a_reply, result );
}


void
DatabaseAPI::aclUpdate( const Auth::ACLUpdateRequest & a_request, Auth::ACLDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"id",a_request.id()});
    if ( a_request.has_rules() )
        params.push_back({"rules",a_request.rules()});

    dbGet( "acl/update", params, result );

    setACLData( a_reply, result );
}

void
DatabaseAPI::aclBySubject( const Auth::ACLBySubjectRequest & a_request,  Auth::ListingReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;

    if ( a_request.has_subject() )
        params.push_back({"subject",a_request.subject()});
    if ( a_request.has_inc_users() )
        params.push_back({"inc_users",a_request.inc_users()?"true":"false"});
    if ( a_request.has_inc_projects() )
        params.push_back({"inc_projects",a_request.inc_projects()?"true":"false"});

    dbGet( "acl/by_subject", params, result );

    setListingDataReply( a_reply, result );
}

void
DatabaseAPI::aclListItemsBySubject( const Auth::ACLListItemsBySubjectRequest & a_request,  Auth::ListingReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;

    params.push_back({"owner",a_request.owner()});
    if ( a_request.has_subject() )
        params.push_back({"subject",a_request.subject()});

    dbGet( "acl/by_subject/list", params, result );

    setListingDataReply( a_reply, result );
}

void
DatabaseAPI::setACLData( ACLDataReply & a_reply, const libjson::Value & a_result )
{
    ACLRule *           rule;

    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const Value::Object & obj = i->asObject();

        rule = a_reply.add_rule();
        rule->set_id( obj.getString( "id" ));

        if ( obj.has( "grant" ))
            rule->set_grant( obj.asNumber() );

        if ( obj.has( "inhgrant" ))
            rule->set_inhgrant( obj.asNumber() );
    }

    TRANSLATE_END( a_result )
}


void
DatabaseAPI::groupCreate( const Auth::GroupCreateRequest & a_request, Auth::GroupDataReply & a_reply )
{
    Value result;

    vector<pair<string,string>> params;
    params.push_back({"gid", a_request.group().gid()});
    if ( a_request.group().uid().compare( m_client_uid ) != 0 )
        params.push_back({"proj", a_request.group().uid()});
    if ( a_request.group().has_title() )
        params.push_back({"title", a_request.group().title()});
    if ( a_request.group().has_desc() )
        params.push_back({"desc", a_request.group().desc()});
    if ( a_request.group().member_size() > 0 )
    {
        string members = "[";
        for ( int i = 0; i < a_request.group().member_size(); ++i )
        {
            if ( i > 0 )
                members += ",";
            members += "\"" + a_request.group().member(i) + "\"";
        }
        members += "]";
        params.push_back({"members",  members });
    }

    dbGet( "grp/create", params, result );

    setGroupData( a_reply, result );
}

void
DatabaseAPI::groupUpdate( const Auth::GroupUpdateRequest & a_request, Auth::GroupDataReply & a_reply )
{
    Value result;

    vector<pair<string,string>> params;
    params.push_back({"gid", a_request.gid()});
    if ( a_request.uid().compare( m_client_uid ) != 0 )
        params.push_back({"proj", a_request.uid()});
    if ( a_request.has_title() )
        params.push_back({"title", a_request.title()});
    if ( a_request.has_desc() )
        params.push_back({"desc", a_request.desc()});
    if ( a_request.add_uid_size() > 0 )
    {
        string members = "[";
        for ( int i = 0; i < a_request.add_uid_size(); ++i )
        {
            if ( i > 0 )
                members += ",";
            members += "\"" + a_request.add_uid(i) + "\"";
        }
        members += "]";
        params.push_back({"add",  members });
    }
    if ( a_request.rem_uid_size() > 0 )
    {
        string members = "[";
        for ( int i = 0; i < a_request.rem_uid_size(); ++i )
        {
            if ( i > 0 )
                members += ",";
            members += "\"" + a_request.rem_uid(i) + "\"";
        }
        members += "]";
        params.push_back({"rem",  members });
    }

    dbGet( "grp/update", params, result );

    setGroupData( a_reply, result );
}

void
DatabaseAPI::groupDelete( const Auth::GroupDeleteRequest & a_request, Anon::AckReply & a_reply )
{
    (void) a_reply;
    Value result;

    vector<pair<string,string>> params;
    params.push_back({"gid", a_request.gid()});
    if ( a_request.uid().compare( m_client_uid ) != 0 )
        params.push_back({"proj", a_request.uid()});

    dbGet( "grp/delete", params, result );
}

void
DatabaseAPI::groupList( const Auth::GroupListRequest & a_request, Auth::GroupDataReply & a_reply )
{
    (void) a_request;

    Value result;
    vector<pair<string,string>> params;
    if ( a_request.uid().compare( m_client_uid ) != 0 )
        params.push_back({"proj", a_request.uid()});

    dbGet( "grp/list", params, result );

    setGroupData( a_reply, result );
}

void
DatabaseAPI::groupView( const Auth::GroupViewRequest & a_request, Auth::GroupDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"gid", a_request.gid()});
    if ( a_request.uid().compare( m_client_uid ) != 0 )
        params.push_back({"proj", a_request.uid()});

    dbGet( "grp/view", params, result );

    setGroupData( a_reply, result );
}

void
DatabaseAPI::setGroupData( GroupDataReply & a_reply, const libjson::Value & a_result )
{
    GroupData *             group;
    Value::ArrayConstIter   j;

    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const Value::Object & obj = i->asObject();

        group = a_reply.add_group();
        group->set_gid( obj.getString( "gid" ));

        if ( obj.has( "uid" ) && !obj.value().isNull() )
            group->set_uid( obj.asString() );

        if ( obj.has( "title" ) && !obj.value().isNull() )
            group->set_title( obj.asString() );

        if ( obj.has( "desc" ) && !obj.value().isNull() )
            group->set_desc( obj.asString() );

        if ( obj.has( "members" ))
        {
            const Value::Array & arr2 = obj.asArray();

            for ( j = arr2.begin(); j != arr2.end(); j++ )
                group->add_member( j->asString() );
        }
    }

    TRANSLATE_END( a_result )
}

void
DatabaseAPI::repoList( const Auth::RepoListRequest & a_request, Auth::RepoDataReply  & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    if ( a_request.has_all() )
        params.push_back({"all", a_request.all()?"true":"false"});
    if ( a_request.has_details() )
        params.push_back({"details", a_request.details()?"true":"false"});

    dbGet( "repo/list", params, result );

    setRepoData( &a_reply, 0, result );
}

void
DatabaseAPI::repoList( std::vector<RepoData*> & a_repos )
{
    Value result;

    dbGet( "repo/list", {{"all","true"},{"details","true"}}, result );

    setRepoData( 0, &a_repos, result );
}

void
DatabaseAPI::repoView( const Auth::RepoViewRequest & a_request, Auth::RepoDataReply  & a_reply )
{
    Value result;

    dbGet( "repo/view", {{"id",a_request.id()}}, result );

    setRepoData( &a_reply, 0, result );
}

void
DatabaseAPI::repoCreate( const Auth::RepoCreateRequest & a_request, Auth::RepoDataReply  & a_reply )
{
    Value result;

    string body = "{\"id\":\"" + a_request.id() + "\"";
    body += ",\"title\":\"" + escapeJSON( a_request.title() ) + "\"";
    body += ",\"path\":\"" + escapeJSON( a_request.path() ) + "\"";
    body += ",\"pub_key\":\"" + escapeJSON( a_request.pub_key() ) + "\"";
    body += ",\"address\":\"" + a_request.address() + "\"";
    body += ",\"endpoint\":\"" + a_request.endpoint() + "\"";
    body += ",\"capacity\":\"" + to_string( a_request.capacity() )+ "\"";

    if ( a_request.has_desc() )
        body += ",\"desc\":\"" + escapeJSON( a_request.desc() ) + "\"";
    if ( a_request.has_domain() )
        body += ",\"domain\":\"" + a_request.domain() + "\"";
    if ( a_request.has_exp_path() )
        body += ",\"exp_path\":\"" + escapeJSON( a_request.exp_path() ) + "\"";

    if ( a_request.admin_size() > 0 )
    {
        body+=",\"admins\":[";
        for ( int i = 0; i < a_request.admin_size(); ++i )
        {
            if ( i > 0 )
                body += ",";
            body += "\"" + a_request.admin(i) + "\"";
        }
        body += "]";
    }
    body += "}";

    dbPost( "repo/create", {}, &body, result );

    setRepoData( &a_reply, 0, result );
}

void
DatabaseAPI::repoUpdate( const Auth::RepoUpdateRequest & a_request, Auth::RepoDataReply  & a_reply )
{
    Value result;

    string body = "{\"id\":\"" + a_request.id() + "\"";
    if ( a_request.has_title() )
        body += ",\"title\":\"" + escapeJSON( a_request.title() ) + "\"";
    if ( a_request.has_desc() )
        body += ",\"desc\":\"" + escapeJSON( a_request.desc() ) + "\"";
    if ( a_request.has_path() )
        body += ",\"path\":\"" + escapeJSON( a_request.path() ) + "\"";
    if ( a_request.has_exp_path() )
        body += ",\"exp_path\":\"" + escapeJSON( a_request.exp_path() ) + "\"";
    if ( a_request.has_domain() )
        body += ",\"domain\":\"" + a_request.domain() + "\"";
    if ( a_request.has_pub_key() )
        body += ",\"pub_key\":\"" + escapeJSON( a_request.pub_key() ) + "\"";
    if ( a_request.has_address() )
        body += ",\"address\":\"" + a_request.address() + "\"";
    if ( a_request.has_endpoint() )
        body += ",\"endpoint\":\"" + a_request.endpoint() + "\"";
    if ( a_request.has_capacity() )
        body += ",\"capacity\":\"" + to_string( a_request.capacity() )+ "\"";

    if ( a_request.admin_size() > 0 )
    {
        body+=",\"admins\":[";
        for ( int i = 0; i < a_request.admin_size(); ++i )
        {
            if ( i > 0 )
                body += ",";
            body += "\"" + a_request.admin(i) + "\"";
        }
        body += "]";
    }
    body += "}";

    dbPost( "repo/update", {}, &body, result );

    setRepoData( &a_reply, 0, result );
}

void
DatabaseAPI::repoDelete( const Auth::RepoDeleteRequest & a_request, Anon::AckReply  & a_reply )
{
    (void) a_reply;
    Value result;

    dbGet( "repo/delete", {{"id",a_request.id()}}, result );
}

void
DatabaseAPI::repoCalcSize( const Auth::RepoCalcSizeRequest & a_request, Auth::RepoCalcSizeReply  & a_reply )
{
    Value result;

    string items = "[";
    if ( a_request.item_size() > 0 )
    {
        for ( int i = 0; i < a_request.item_size(); ++i )
        {
            if ( i > 0 )
                items += ",";
            items += "\"" + a_request.item(i) + "\"";
        }
        items += "]";
    }

    dbGet( "repo/calc_size", {{"recurse",a_request.recurse()?"true":"false"},{"items",items}}, result );

    TRANSLATE_BEGIN()

    const Value::Array & arr = result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        setAllocStatsData( *a_reply.add_stats(), i->asObject() );
    }

    TRANSLATE_END( result )
}


void
DatabaseAPI::setRepoData( Auth::RepoDataReply * a_reply, std::vector<RepoData*> * a_repos, const libjson::Value & a_result )
{
    if ( !a_reply && !a_repos )
        EXCEPT( ID_INTERNAL_ERROR, "Missing parameters" );

    RepoData *          repo;
    Value::ArrayConstIter    k;

    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        const Value::Object & obj = i->asObject();

        if ( a_reply )
            repo = a_reply->add_repo();
        else
            repo = new RepoData();

        repo->set_id( obj.getString( "id" ));

        if ( obj.has( "title" ))
            repo->set_title( obj.asString() );

        if ( obj.has( "desc" ))
            repo->set_desc( obj.asString() );

        if ( obj.has( "capacity" ))
            repo->set_capacity( obj.asNumber() ); // TODO Needs to be 64 bit integer (string in JSON)

        if ( obj.has( "address" ))
            repo->set_address( obj.asString() );

        if ( obj.has( "endpoint" ))
            repo->set_endpoint( obj.asString() );

        if ( obj.has( "pub_key" ))
            repo->set_pub_key( obj.asString() );

        if ( obj.has( "path" ))
            repo->set_path( obj.asString() );

        if ( obj.has( "exp_path" ))
            repo->set_exp_path( obj.asString() );

        if ( obj.has( "domain" ) && !obj.value().isNull( ))
            repo->set_domain( obj.asString() );

        if ( obj.has( "admins" ))
        {
            const Value::Array & arr2 = obj.asArray();

            for ( k = arr2.begin(); k != arr2.end(); k++ )
            {
                repo->add_admin( k->asString() );
            }
        }

        if ( a_repos )
            a_repos->push_back( repo );
    }

    TRANSLATE_END( a_result )
}

void
DatabaseAPI::repoListAllocations( const Auth::RepoListAllocationsRequest & a_request, Auth::RepoAllocationsReply  & a_reply )
{
    Value result;

    dbGet( "repo/alloc/list/by_repo", {{"repo",a_request.id()}}, result );

    setAllocData( a_reply, result );
}

void
DatabaseAPI::repoListSubjectAllocations( const Auth::RepoListSubjectAllocationsRequest & a_request, Auth::RepoAllocationsReply  & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    if ( a_request.has_subject() )
        params.push_back({"owner",a_request.subject()});
    else
        params.push_back({"owner",m_client_uid});
    if ( a_request.has_stats() )
        params.push_back({"stats",a_request.stats()?"true":"false"});

    dbGet( "repo/alloc/list/by_owner", params, result );

    setAllocData( a_reply, result );
}

void
DatabaseAPI::repoListObjectAllocations( const Auth::RepoListObjectAllocationsRequest & a_request, Auth::RepoAllocationsReply  & a_reply )
{
    Value result;

    dbGet( "repo/alloc/list/by_object", {{"object",a_request.id()}}, result );

    setAllocData( a_reply, result );
}


void
DatabaseAPI::setAllocData( Auth::RepoAllocationsReply & a_reply, const libjson::Value & a_result )
{
    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();

    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        setAllocData( a_reply.add_alloc(), i->asObject() );
    }

    TRANSLATE_END( a_result )
}


void
DatabaseAPI::setAllocData( AllocData * a_alloc, const libjson::Value::Object & a_obj )
{
    a_alloc->set_repo( a_obj.getString( "repo" ));
    a_alloc->set_data_limit( a_obj.getNumber( "data_limit" ));
    a_alloc->set_data_size( a_obj.getNumber( "data_size" ));
    a_alloc->set_rec_limit( a_obj.getNumber( "rec_limit" ));
    a_alloc->set_rec_count( a_obj.getNumber( "rec_count" ));
    a_alloc->set_path( a_obj.getString( "path" ));

    if ( a_obj.has( "is_def" ))
        a_alloc->set_is_def( a_obj.asBool() );

    if ( a_obj.has( "id" ))
        a_alloc->set_id( a_obj.asString() );

    if ( a_obj.has( "stats" ))
        setAllocStatsData( *a_alloc->mutable_stats( ), a_obj );
}

void
DatabaseAPI::repoViewAllocation( const Auth::RepoViewAllocationRequest & a_request, Auth::RepoAllocationsReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"repo",a_request.repo()});
    if ( a_request.has_subject() )
        params.push_back({"subject",a_request.subject()});

    dbGet( "repo/alloc/view", params, result );

    setAllocData( a_reply, result );
}

void
DatabaseAPI::repoAllocationStats( const Auth::RepoAllocationStatsRequest & a_request, Auth::RepoAllocationStatsReply  & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({"repo",a_request.repo()});
    if ( a_request.has_subject() )
        params.push_back({"subject",a_request.subject()});

    dbGet( "repo/alloc/stats", params, result );

    TRANSLATE_BEGIN()

    setAllocStatsData( *a_reply.mutable_alloc(), result.asObject() );

    TRANSLATE_END( result )
}


void
DatabaseAPI::setAllocStatsData( AllocStatsData & a_stats, const libjson::Value::Object & a_obj )
{
    a_stats.set_repo( a_obj.getString( "repo" ));
    a_stats.set_rec_count( a_obj.getNumber( "rec_count" ));
    a_stats.set_file_count( a_obj.getNumber( "file_count" ));
    a_stats.set_data_size( a_obj.getNumber( "data_size" ));

    if ( a_obj.has( "histogram" ))
    {
        const Value::Array & arr = a_obj.asArray();

        for ( Value::ArrayConstIter j = arr.begin(); j != arr.end(); j++ )
            a_stats.add_histogram( j->asNumber() );
    }
}

void
DatabaseAPI::repoAllocationSet( const Auth::RepoAllocationSetRequest & a_request, Anon::AckReply  & a_reply )
{
    (void)a_reply;
    Value result;

    dbGet( "repo/alloc/set", {{"repo",a_request.repo()},{"subject",a_request.subject()},{"data_limit",to_string(a_request.data_limit())},{"rec_limit",to_string(a_request.rec_limit())}}, result );
}

void
DatabaseAPI::repoAllocationSetDefault( const Auth::RepoAllocationSetDefaultRequest & a_request, Anon::AckReply  & a_reply )
{
    (void)a_reply;

    Value result;
    vector<pair<string,string>> params;

    params.push_back({"repo",a_request.repo()});
    if ( a_request.has_subject() )
        params.push_back({"subject",a_request.subject()});

    dbGet( "repo/alloc/set/default", params, result );
}

void
DatabaseAPI::checkPerms( const CheckPermsRequest & a_request, CheckPermsReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({ "id", a_request.id()});
    if ( a_request.has_perms() )
        params.push_back({ "perms", to_string( a_request.perms()) });

    dbGet( "authz/perm/check", params, result );

    TRANSLATE_BEGIN()

    a_reply.set_granted( result.asObject().getBool( "granted" ));

    TRANSLATE_END( result )
}

void
DatabaseAPI::getPerms( const GetPermsRequest & a_request, GetPermsReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({ "id", a_request.id()});
    if ( a_request.has_perms() )
        params.push_back({ "perms", to_string( a_request.perms()) });

    dbGet( "authz/perm/get", params, result );

    TRANSLATE_BEGIN()

    a_reply.set_granted( result.asObject().getNumber( "granted" ));

    TRANSLATE_END( result )
}

void
DatabaseAPI::repoAuthz( const Auth::RepoAuthzRequest & a_request, Anon::AckReply  & a_reply )
{
    (void)a_reply;
    Value result;

    dbGet( "authz/gridftp", {{"repo",a_request.repo()},{"file",a_request.file()},{"act",a_request.action()}}, result );
}

void
DatabaseAPI::topicList( const Auth::TopicListRequest & a_request, Auth::ListingReply  & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    if ( a_request.has_topic_id() )
        params.push_back({ "id", a_request.topic_id() });
    if ( a_request.has_data() )
        params.push_back({ "data", a_request.data()?"true":"false" });
    if ( a_request.has_offset() && a_request.has_count() )
    {
        params.push_back({ "offset", to_string( a_request.offset() )});
        params.push_back({ "count", to_string( a_request.count() )});
    }

    dbGet( "topic/list", params, result );

    setListingDataReply( a_reply, result );
}


void
DatabaseAPI::topicLink( const Auth::TopicLinkRequest & a_request, Anon::AckReply  & a_reply )
{
    (void) a_reply;
    Value result;

    dbGet( "topic/link", {{ "topic", a_request.topic() },{ "id", a_request.id() }}, result );
}


void
DatabaseAPI::topicUnlink( const Auth::TopicUnlinkRequest & a_request, Anon::AckReply  & a_reply )
{
    (void) a_reply;
    Value result;

    dbGet( "topic/unlink", {{ "topic", a_request.topic() },{ "id", a_request.id() }}, result );
}


void
DatabaseAPI::annotationCreate( const AnnotationCreateRequest & a_request, AnnotationDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({ "type", to_string( a_request.type() )});
    params.push_back({ "subject", a_request.subject() });
    params.push_back({ "title", a_request.title() });
    params.push_back({ "comment", a_request.comment() });
    params.push_back({ "activate", a_request.activate()?"true":"false" });

    dbPost( "note/create", params, 0, result );

    setNoteDataReply( a_reply, result );
}

void
DatabaseAPI::annotationUpdate( const AnnotationUpdateRequest & a_request, AnnotationDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({ "id", a_request.id() });
    params.push_back({ "comment", a_request.comment() });
    if ( a_request.has_new_type() )
        params.push_back({ "new_type", to_string( a_request.new_type() )});
    if ( a_request.has_new_state() )
        params.push_back({ "new_state", to_string( a_request.new_state() )});
    if ( a_request.has_new_title() )
        params.push_back({ "new_title", a_request.new_title() });

    dbPost( "note/update", params, 0, result );

    setNoteDataReply( a_reply, result );
}

void
DatabaseAPI::annotationCommentEdit( const Auth::AnnotationCommentEditRequest & a_request, Auth::AnnotationDataReply & a_reply )
{
    Value result;
    vector<pair<string,string>> params;
    params.push_back({ "id", a_request.id() });
    params.push_back({ "comment", a_request.comment() });
    params.push_back({ "comment_idx", to_string( a_request.comment_idx() )});

    dbPost( "note/comment/edit", params, 0, result );

    setNoteDataReply( a_reply, result );
}

void
DatabaseAPI::annotationView( const AnnotationViewRequest & a_request, AnnotationDataReply & a_reply )
{
    Value result;

    dbGet( "note/view", {{"id",a_request.id()}}, result );

    setNoteDataReply( a_reply, result );
}

void
DatabaseAPI::annotationListBySubject( const AnnotationListBySubjectRequest & a_request, AnnotationDataReply & a_reply )
{
    Value result;

    dbGet( "note/list/by_subject", {{"subject",a_request.subject()}}, result );

    setNoteDataReply( a_reply, result );
}

void
DatabaseAPI::annotationPurge( uint32_t a_age_sec )
{
    Value result;

    dbGet( "note/purge", {{"age_sec",to_string( a_age_sec )}}, result );
}

void
DatabaseAPI::setNoteDataReply( Auth::AnnotationDataReply & a_reply, const libjson::Value & a_result )
{
    Value::ArrayConstIter    i;

    TRANSLATE_BEGIN()

    const Value::Object & res_obj = a_result.asObject();

    if ( res_obj.has( "results" ))
    {
        const Value::Array & arr = res_obj.asArray();

        for ( i = arr.begin(); i != arr.end(); i++ )
            setNoteData( a_reply.add_note(), i->asObject() );
    }

    if ( res_obj.has( "updates" ))
    {
        const Value::Array & arr = res_obj.asArray();

        for ( i = arr.begin(); i != arr.end(); i++ )
            setListingData( a_reply.add_update(), i->asObject() );
    }

    TRANSLATE_END( a_result )
}

void
DatabaseAPI::setNoteData( NoteData * a_note, const libjson::Value::Object & a_obj )
{
    a_note->set_id( a_obj.getString( "_id" ));
    a_note->set_type((NoteType) a_obj.getNumber( "type" ));
    a_note->set_state((NoteState) a_obj.getNumber( "state" ));
    a_note->set_subject_id( a_obj.getString( "subject_id" ));
    a_note->set_title( a_obj.getString( "title" ));
    a_note->set_ct( a_obj.getNumber( "ct" ));
    a_note->set_ut( a_obj.getNumber( "ut" ));

    if ( a_obj.has( "parent_id" ) && !a_obj.value().isNull( ))
        a_note->set_parent_id( a_obj.asString() );

    if ( a_obj.has( "has_child" ))
        a_note->set_has_child( a_obj.asBool() );

    if ( a_obj.has( "comments" ))
    {
        const Value::Array &      arr = a_obj.asArray();
        Value::ObjectIter   m;
        NoteComment *       comment;

        for ( Value::ArrayConstIter k = arr.begin(); k != arr.end(); k++ )
        {
            const Value::Object & obj = k->asObject();

            comment = a_note->add_comment();
            comment->set_user( obj.getString( "user" ));
            comment->set_time( obj.getNumber( "time" ));
            comment->set_comment( obj.getString( "comment" ));

            if ( obj.has( "new_type" ) && !obj.value().isNull( ))
                comment->set_type((NoteType) obj.asNumber() );
            if ( obj.has( "new_state" ) && !obj.value().isNull( ))
                comment->set_state((NoteState) obj.asNumber() );
        }
    }
}

void
DatabaseAPI::taskLoadRunnable( libjson::Value & a_result )
{
    dbGet( "task/runnable", {}, a_result );
}


void
DatabaseAPI::taskRun( const std::string & a_task_id, libjson::Value & a_task_reply, int * a_step, std::string * a_err_msg )
{
    vector<pair<string,string>> params;
    params.push_back({"task_id",a_task_id});
    if ( a_step )
        params.push_back({ "step", to_string( *a_step )});
    if ( a_err_msg )
        params.push_back({ "err_msg", *a_err_msg });

    dbGet( "task/run", params, a_task_reply );
}


void
DatabaseAPI::taskAbort( const std::string & a_task_id, const std::string & a_msg, libjson::Value & a_task_reply )
{
    libjson::Value doc = a_msg;
    string body = doc.toString();

    dbPost( "task/abort", {{"task_id",a_task_id}}, &body, a_task_reply );
}


void
DatabaseAPI::taskInitDataGet( const Auth::DataGetRequest & a_request, Auth::DataGetPutReply & a_reply, libjson::Value & a_result )
{
    string body = "{\"id\":[";

    for ( int i = 0; i < a_request.id_size(); i++ )
    {
        if ( i > 0 )
            body += ",";

        body += "\"" + a_request.id(i) + "\"";
    }
    body += "]";

    if ( a_request.has_path() )
        body += ",\"path\":\"" + a_request.path() + "\"";

    if ( a_request.has_encrypt() )
        body += ",\"encrypt\":" + to_string( a_request.encrypt() );

    if ( a_request.has_orig_fname() && a_request.orig_fname() )
        body += ",\"orig_fname\":true";

    if ( a_request.has_check() && a_request.check() )
        body += ",\"check\":true";

    body += "}";

    dbPost( "dat/get", {}, &body, a_result );

    setDataGetSetReply( a_reply, a_result );
}


void
DatabaseAPI::taskInitDataPut( const Auth::DataPutRequest & a_request, Auth::DataGetPutReply & a_reply, libjson::Value & a_result )
{
    string body = "{\"id\":[\"" + a_request.id() + "\"]";

    if ( a_request.has_path() )
        body += ",\"path\":\"" + a_request.path() + "\"";

    if ( a_request.has_encrypt() )
        body += ",\"encrypt\":" + to_string( a_request.encrypt() );

    if ( a_request.has_ext() )
        body += ",\"ext\":\"" + a_request.ext() + "\"";

    if ( a_request.has_check() && a_request.check() )
        body += ",\"check\":true";

    body += "}";

    dbPost( "dat/put", {}, &body, a_result );

    setDataGetSetReply( a_reply, a_result );
}

void
DatabaseAPI::setDataGetSetReply( Auth::DataGetPutReply & a_reply, const libjson::Value & a_result )
{
    Value::ObjectIter   t;

    TRANSLATE_BEGIN()

    const Value::Object &   obj = a_result.asObject();
    Value::ObjectIter   i;
    Value::ArrayConstIter   j;

    if ( obj.has( "glob_data" ) && obj.value().size() )
    {
        const Value::Array & arr = obj.asArray();

        for ( j = arr.begin(); j != arr.end(); j++ )
            setListingData( a_reply.add_item(), j->asObject() );
    }

    if ( obj.has( "http_data" ) && obj.value().size() )
    {
        const Value::Array & arr = obj.asArray();

        for ( j = arr.begin(); j != arr.end(); j++ )
            setListingData( a_reply.add_item(), j->asObject() );
    }

    if ( obj.has( "task" ))
        setTaskData( a_reply.mutable_task(), obj.value() );

    TRANSLATE_END( a_result )
}

void
DatabaseAPI::taskInitRecordCollectionDelete( const std::vector<std::string> & a_ids, TaskDataReply & a_reply, libjson::Value & a_result )
{
    string body = "{\"ids\":[";

    for ( vector<string>::const_iterator i = a_ids.begin(); i != a_ids.end(); i++ )
    {
        if ( i != a_ids.begin() )
            body += ",";

        body += "\"" + *i + "\"";
    }
    body += "]}";

    dbPost( "dat/delete", {}, &body, a_result );

    setTaskDataReply( a_reply, a_result );
}


void
DatabaseAPI::taskInitRecordAllocChange( const Auth::RecordAllocChangeRequest & a_request, Auth::RecordAllocChangeReply & a_reply, libjson::Value & a_result )
{
    string body = "{\"ids\":[";

    for ( int i = 0; i < a_request.id_size(); i++ )
    {
        if ( i > 0 )
            body += ",";

        body += "\"" + a_request.id( i ) + "\"";
    }
    body += "],\"repo_id\":\"" + a_request.repo_id() + "\"";
    if ( a_request.has_proj_id() )
        body += string(",\"proj_id\":\"") + a_request.proj_id() + "\"";
    if ( a_request.has_check() )
        body += string(",\"check\":\"") + (a_request.check()?"true":"false") + "\"";
    body += "}";

    dbPost( "dat/alloc_chg", {}, &body, a_result );

    TRANSLATE_BEGIN()

    const Value::Object & obj = a_result.asObject();

    a_reply.set_act_cnt( obj.getNumber( "act_cnt" ));
    a_reply.set_act_size( obj.getNumber( "act_size" ));
    a_reply.set_tot_cnt( obj.getNumber( "tot_cnt" ));
    a_reply.set_data_limit( obj.getNumber( "data_limit" ));
    a_reply.set_data_size( obj.getNumber( "data_size" ));
    a_reply.set_rec_limit( obj.getNumber( "rec_limit" ));
    a_reply.set_rec_count( obj.getNumber( "rec_count" ));

    if ( obj.has( "task" ))
        setTaskData( a_reply.mutable_task(), obj.value() );

    TRANSLATE_END( a_result )
}


void
DatabaseAPI::taskInitRecordOwnerChange( const Auth::RecordOwnerChangeRequest & a_request, Auth::RecordOwnerChangeReply & a_reply, libjson::Value & a_result )
{
    string body = "{\"ids\":[";

    for ( int i = 0; i < a_request.id_size(); i++ )
    {
        if ( i > 0 )
            body += ",";

        body += "\"" + a_request.id( i ) + "\"";
    }
    body += "],\"coll_id\":\"" + a_request.coll_id() + "\"";
    if ( a_request.has_repo_id() )
        body += ",\"repo_id\":\"" + a_request.repo_id() + "\"";
    //if ( a_request.has_proj_id() )
    //    body += string(",\"proj_id\":\"") + a_request.proj_id() + "\"";
    if ( a_request.has_check() )
        body += string(",\"check\":\"") + (a_request.check()?"true":"false") + "\"";
    body += "}";

    dbPost( "dat/owner_chg", {}, &body, a_result );

    TRANSLATE_BEGIN()

    const Value::Object & obj = a_result.asObject();

    a_reply.set_act_cnt( obj.getNumber( "act_cnt" ));
    a_reply.set_act_size( obj.getNumber( "act_size" ));
    a_reply.set_tot_cnt( obj.getNumber( "tot_cnt" ));

    if ( obj.has( "allocs" ))
    {
        const Value::Array & arr = obj.asArray();

        for ( Value::ArrayConstIter a = arr.begin(); a != arr.end(); a++ )
            setAllocData( a_reply.add_alloc(), a->asObject() );
    }

    if ( obj.has( "task" ))
        setTaskData( a_reply.mutable_task(), obj.value() );

    TRANSLATE_END( a_result )
}


void
DatabaseAPI::taskInitProjectDelete( const Auth::ProjectDeleteRequest & a_request, Auth::TaskDataReply & a_reply, libjson::Value & a_result )
{
    string body = "{\"ids\":[";

    for ( int i = 0; i < a_request.id_size(); i++ )
    {
        if ( i > 0 )
            body += ",";

        body += "\"" + a_request.id(i) + "\"";
    }
    body += "]}";

    dbPost( "prj/delete", {}, &body, a_result );

    setTaskDataReply( a_reply, a_result );
}


void
DatabaseAPI::taskInitRepoAllocationCreate( const Auth::RepoAllocationCreateRequest & a_request, Auth::TaskDataReply & a_reply, libjson::Value & a_result )
{
    dbGet( "repo/alloc/create", {{"subject",a_request.subject()},{"repo",a_request.repo()},
        {"data_limit",to_string(a_request.data_limit())},{"rec_limit",to_string(a_request.rec_limit())}}, a_result );

    setTaskDataReply( a_reply, a_result );
}


void
DatabaseAPI::taskInitRepoAllocationDelete( const Auth::RepoAllocationDeleteRequest & a_request, Auth::TaskDataReply & a_reply, libjson::Value & a_result )
{
    dbGet( "repo/alloc/delete", {{"subject",a_request.subject()},{"repo",a_request.repo()}}, a_result );

    setTaskDataReply( a_reply, a_result );
}


void
DatabaseAPI::setTaskData( TaskData * a_task, const libjson::Value & a_task_json )
{
    const Value::Object & obj = a_task_json.asObject();

    a_task->set_id( obj.getString( "_id" ));
    a_task->set_type((TaskType) obj.getNumber( "type" ));
    a_task->set_status((TaskStatus) obj.getNumber( "status" ));
    a_task->set_client( obj.getString( "client" ));
    int step = obj.getNumber( "step" );
    a_task->set_step( step < 0?-step:step);
    a_task->set_steps( obj.getNumber( "steps" ));
    a_task->set_msg( obj.getString( "msg" ));
    a_task->set_ct( obj.getNumber( "ct" ));
    a_task->set_ut( obj.getNumber( "ut" ));
}


/**
 * @brief Sets TaskDataReply from JSON returned by a taskInit... call
 * @param a_reply 
 * @param a_result 
 *
 * JSON contains an object with a "task" field containing task fields. This
 * method removes tasks that are nor in READY status from the original JSON
 * input - this is to.
 */
void
DatabaseAPI::setTaskDataReply( Auth::TaskDataReply & a_reply, const libjson::Value & a_result )
{
    TRANSLATE_BEGIN()

    const Value::Object & obj = a_result.asObject();

    if ( obj.has( "task" ))
        setTaskData( a_reply.add_task(), obj.value() );

    TRANSLATE_END( a_result )
}



/**
 * @brief Sets TaskDataReply from JSON returned by a task management call
 * @param a_reply 
 * @param a_result 
 *
 * JSON contains an array of task objects containing task fields.
 */
void
DatabaseAPI::setTaskDataReplyArray( Auth::TaskDataReply & a_reply, const libjson::Value & a_result )
{
    TRANSLATE_BEGIN()

    const Value::Array & arr = a_result.asArray();
    for ( Value::ArrayConstIter i = arr.begin(); i != arr.end(); i++ )
    {
        setTaskData( a_reply.add_task(), *i );
    }

    TRANSLATE_END( a_result )
}


void
DatabaseAPI::taskStart( const std::string & a_task_id, libjson::Value & a_result )
{
    dbGet( "task/start", {{"task_id",a_task_id}}, a_result );
}


void
DatabaseAPI::taskUpdate( const std::string & a_id, TaskStatus * a_status, const std::string * a_message, double * a_progress, libjson::Value * a_state )
{
    if ( !a_status && !a_progress && !a_state )
        return;

    string body = "{";
    string delim = "";

    if ( a_status )
    {
        body += "\"status\":" + to_string(*a_status);
        delim = ",";
    }

    if ( a_message )
    {
        body += delim + "\"message\":\"" + *a_message + "\"";
        if ( !delim.size( ))
            delim = ",";
    }

    if ( a_progress )
    {
        body += delim + "\"progress\":" + to_string(*a_progress);
        if ( !delim.size( ))
            delim = ",";
    }

    if ( a_state )
    {
        body += delim + "\"state\":" + a_state->toString();
    }

    body += "}";

    Value result;
    dbPost( "task/update", {{"task_id",a_id}}, &body, result );
}


void
DatabaseAPI::taskFinalize( const std::string & a_task_id, bool a_succeeded, const std::string & a_msg, libjson::Value & a_result )
{
    vector<pair<string,string>> params;
    params.push_back({ "task_id", a_task_id });
    params.push_back({ "succeeded", ( a_succeeded?"true":"false" )});
    if ( a_msg.size( ))
        params.push_back({ "message", a_msg });

    dbPost( "task/finalize", params, 0, a_result );
}


void
DatabaseAPI::taskList( const Auth::TaskListRequest & a_request, Auth::TaskDataReply & a_reply )
{
    vector<pair<string,string>> params;

    if ( a_request.has_proj_id( ))
        params.push_back({ "proj_id", a_request.proj_id() });
    if ( a_request.has_since( ))
        params.push_back({ "since", to_string( a_request.since() )});
    if ( a_request.has_from( ))
        params.push_back({ "from", to_string( a_request.from() )});
    if ( a_request.has_to( ))
        params.push_back({ "to", to_string( a_request.to() )});
    if ( a_request.has_offset( ))
        params.push_back({ "offset", to_string( a_request.offset() )});
    if ( a_request.has_count( ))
        params.push_back({ "count", to_string( a_request.count() )});
    if ( a_request.status_size() > 0 )
    {
        string stat = "[";
        for ( int i = 0; i < a_request.status_size(); ++i )
        {
            if ( i > 0 )
                stat += ",";
            stat += to_string(a_request.status(i));
        }
        stat += "]";
        params.push_back({ "status",stat});
    }

    libjson::Value result;

    dbGet( "task/list", params, result );

    setTaskDataReplyArray( a_reply, result );
}

void
DatabaseAPI::taskView( const Auth::TaskViewRequest & a_request, Auth::TaskDataReply & a_reply )
{
    libjson::Value result;

    dbGet( "task/view", {{"task_id",a_request.task_id()}}, result );

    setTaskDataReplyArray( a_reply, result );
}

void
DatabaseAPI::taskPurge( uint32_t a_age_sec )
{
    libjson::Value result;

    dbGet( "task/purge", {{"age_sec",to_string( a_age_sec )}}, result );
}

}}
