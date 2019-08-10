/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __KIS_NET_MICROHTTPD__
#define __KIS_NET_MICROHTTPD__

#include "config.h"

#include <atomic>
#include <stdio.h>
#include <time.h>
#include <list>
#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <sstream>
#include <microhttpd.h>
#include <memory>

#include "globalregistry.h"
#include "kis_mutex.h"
#include "kis_net_microhttpd_handlers.h"
#include "structured.h"
#include "trackedelement.h"

class kis_net_httpd;
class kis_net_httpd_session;
class kis_net_httpd_connection;
class kis_net_httpd_handler;

class entry_tracker;

namespace kishttpd {
    std::string get_suffix(const std::string& path);
    std::string strip_suffix(const std::string& path);
    std::string escape_html(const std::string& path);

    // Summarize based on a summarization dictionary, if one is present.
    // MAY THROW EXCEPTIONS if summarization is malformed.
    // Calls the standard, nested/vectorization summarization if passed a vector, single summarization
    // if passed a map/trackedcomponent object.
    // Modifies the rename_map field, which must be provided by the caller.
    // Returns a summarized vector (if passed a vector) or summarized device (if passed
    // a summarized device)
    std::shared_ptr<tracker_element> summarize_with_structured(std::shared_ptr<tracker_element> in_data,
            shared_structured structured, std::shared_ptr<tracker_element_serializer::rename_map> rename_map);
};

// Connection data, generated for all requests by the processing system;
// contains per-handler states, request information, request type, session
// data if known, POST variables if the standard POST processing is enabled
class kis_net_httpd_connection {
public:
    using variable_cache_map = std::map<std::string, std::shared_ptr<std::stringstream>>;

    const static int CONNECTION_GET = 0;
    const static int CONNECTION_POST = 1;

    kis_net_httpd_connection() {
        httpcode = 200;
        postprocessor = NULL;
        post_complete = false;
        connection_type = CONNECTION_GET;
        httpd = NULL;
        httpdhandler = NULL;
        session = NULL;
        connection = NULL;
        response = NULL;
        custom_extension = NULL;
    }

    // response generated by post
    std::stringstream response_stream;

    // Cache of variables in session
    variable_cache_map variable_cache;

    bool has_cached_variable(const std::string& key) {
        return (variable_cache.find(key) != variable_cache.end());
    }

    template <typename T>
    T variable_cache_as(const std::string& key) {
        T t;

        auto v = variable_cache.find(key);

        if (v == variable_cache.end())
            throw std::runtime_error(fmt::format("variable '{}' not found", kishttpd::escape_html(key)));

        *v->second >> t;

        if (v->second->fail())
            throw std::runtime_error(fmt::format("unable to convert value of '{}'", kishttpd::escape_html(key)));

        return t;
    }

    // Optional alternate filename to pass to the browser for downloading
    std::string optional_filename;

    // HTTP code of response
    int httpcode;

    // URL
    std::string url;

    // URL component considered for mime typing
    std::string mime_url;

    // Post processor struct
    struct MHD_PostProcessor *postprocessor;

    // Is the post complete?
    bool post_complete;

    // Type of request/connection
    int connection_type;

    // httpd parent
    kis_net_httpd *httpd;    

    // Handler
    kis_net_httpd_handler *httpdhandler;    

    // Login session
    std::shared_ptr<kis_net_httpd_session> session;

    // Connection
    struct MHD_Connection *connection;

    // Response created elsewhere, if any
    struct MHD_Response *response;

    // Custom arbitrary value inserted by other processors
    void *custom_extension;

    // Integrity locker
    std::mutex connection_mutex;
};

class kis_net_httpd_session {
public:
    // Session ID
    std::string sessionid;

    // Time session was created
    time_t session_created;

    // Last time the session was seen active
    time_t session_seen;

    // Amount of time session is valid for after last active
    time_t session_lifetime;
};

class kis_httpd_websession;

// Do a simple dump of a tracked object into an endpoint
class kis_net_httpd_simple_tracked_endpoint : public kis_net_httpd_chain_stream_handler {
public:
    using gen_func = std::function<std::shared_ptr<tracker_element> ()>;

    kis_net_httpd_simple_tracked_endpoint(const std::string& in_uri, 
            std::shared_ptr<tracker_element> in_content, 
            kis_recursive_timed_mutex *in_mutex);
    kis_net_httpd_simple_tracked_endpoint(const std::string& in_uri, gen_func in_func);
    kis_net_httpd_simple_tracked_endpoint(const std::string& in_uri, gen_func in_func,
            kis_recursive_timed_mutex *in_mutex);

    virtual ~kis_net_httpd_simple_tracked_endpoint() { }

    // HTTP handlers
    virtual bool httpd_verify_path(const char *path, const char *method) override;

    virtual int httpd_create_stream_response(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection,
            const char *url, const char *method, const char *upload_data,
            size_t *upload_data_size) override;

    virtual int httpd_post_complete(kis_net_httpd_connection *concls) override;

protected:
    std::string uri;
    std::shared_ptr<tracker_element> content;
    gen_func generator;
    kis_recursive_timed_mutex *mutex;
};

// Do a simple dump of a tracked object into an endpoint, DO NOT require authentication.
// This should be very rarely used.
class kis_net_httpd_simple_unauth_tracked_endpoint : public kis_net_httpd_chain_stream_handler {
public:
    using gen_func = std::function<std::shared_ptr<tracker_element> ()>;

    kis_net_httpd_simple_unauth_tracked_endpoint(const std::string& in_uri, 
            std::shared_ptr<tracker_element> in_content, 
            kis_recursive_timed_mutex *in_mutex);
    kis_net_httpd_simple_unauth_tracked_endpoint(const std::string& in_uri, gen_func in_func);
    kis_net_httpd_simple_unauth_tracked_endpoint(const std::string& in_uri, gen_func in_func,
            kis_recursive_timed_mutex *in_mutex);

    virtual ~kis_net_httpd_simple_unauth_tracked_endpoint() { }

    // HTTP handlers
    virtual bool httpd_verify_path(const char *path, const char *method) override;

    virtual int httpd_create_stream_response(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection,
            const char *url, const char *method, const char *upload_data,
            size_t *upload_data_size) override;

    virtual int httpd_post_complete(kis_net_httpd_connection *concls) override;

protected:
    std::string uri;
    std::shared_ptr<tracker_element> content;
    gen_func generator;
    kis_recursive_timed_mutex *mutex;
};

// A similar simplified endpoint dump but with a callback function for the path processing
// and for the endpoint generation; for more rest-like paths
class kis_net_httpd_path_tracked_endpoint : public kis_net_httpd_chain_stream_handler {
public:
    using gen_func = std::function<std::shared_ptr<tracker_element> (const std::vector<std::string>&)>;
    using path_func = std::function<bool (const std::vector<std::string>&)>;

    kis_net_httpd_path_tracked_endpoint(path_func in_path, gen_func in_gen);
    kis_net_httpd_path_tracked_endpoint(path_func in_path, gen_func in_gen,
            kis_recursive_timed_mutex *in_mutex);
    virtual ~kis_net_httpd_path_tracked_endpoint() { }

    // HTTP handlers
    virtual bool httpd_verify_path(const char *path, const char *method) override;

    virtual int httpd_create_stream_response(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection,
            const char *url, const char *method, const char *upload_data,
            size_t *upload_data_size) override;

    virtual int httpd_post_complete(kis_net_httpd_connection *concls) override;

protected:
    path_func path;
    gen_func generator;
    kis_recursive_timed_mutex *mutex;
};


// Extremely simple callback-based POST responder linked to a chainbuf buffer
class kis_net_httpd_simple_post_endpoint : public kis_net_httpd_chain_stream_handler {
public:
    using handler_func = 
        std::function<unsigned int (std::ostream& stream, const std::string& uri, shared_structured post_structured,
                kis_net_httpd_connection::variable_cache_map& variable_cache)>;

    kis_net_httpd_simple_post_endpoint(const std::string& in_uri, handler_func in_func,
            kis_recursive_timed_mutex *in_mutex);
    kis_net_httpd_simple_post_endpoint(const std::string& in_uri, handler_func in_func);

    virtual ~kis_net_httpd_simple_post_endpoint() { }

    // HTTP handlers
    virtual bool httpd_verify_path(const char *path, const char *method) override;

    virtual int httpd_create_stream_response(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection,
            const char *url, const char *method, const char *upload_data,
            size_t *upload_data_size) override;

    virtual int httpd_post_complete(kis_net_httpd_connection *concls) override;

protected:
    std::string uri;
    handler_func generator;
    kis_recursive_timed_mutex *mutex;
};

// Path-based basic post responder linked to a chainbuf buffer
class Kis_Net_Httpd_Path_Post_Endpoint : public kis_net_httpd_chain_stream_handler {
public:
    using path_func = std::function<bool (const std::vector<std::string>& path, const std::string& uri)>;
    using handler_func = 
        std::function<unsigned int (std::ostream& stream, 
                const std::vector<std::string>& path, const std::string& uri, 
                shared_structured post_structured,
                kis_net_httpd_connection::variable_cache_map& variable_cache)>;

    Kis_Net_Httpd_Path_Post_Endpoint(path_func in_path, handler_func in_func);
    Kis_Net_Httpd_Path_Post_Endpoint(path_func in_path, handler_func in_func,
            kis_recursive_timed_mutex *in_mutex);

    virtual ~Kis_Net_Httpd_Path_Post_Endpoint() { }

    // HTTP handlers
    virtual bool httpd_verify_path(const char *path, const char *method) override;

    virtual int httpd_create_stream_response(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection,
            const char *url, const char *method, const char *upload_data,
            size_t *upload_data_size) override;

    virtual int httpd_post_complete(kis_net_httpd_connection *concls) override;

protected:
    path_func path;
    handler_func generator;
    kis_recursive_timed_mutex *mutex;
};

#define KIS_SESSION_COOKIE      "KISMET"
#define KIS_HTTPD_POSTBUFFERSZ  (1024 * 32)

class kis_net_httpd : public lifetime_global {
public:
    static std::string global_name() { return "HTTPD_SERVER"; }

    static std::shared_ptr<kis_net_httpd> create_httpd() {
        std::shared_ptr<kis_net_httpd> mon(new kis_net_httpd());
        Globalreg::globalreg->register_lifetime_global(mon);
        Globalreg::globalreg->insert_global(global_name(), mon);
        return mon;
    }

private:
    kis_net_httpd();

public:
    virtual ~kis_net_httpd();

    int StartHttpd();
    int StopHttpd();

    bool HttpdRunning() { return running; }
    unsigned int FetchPort() { return http_port; };
    bool FetchUsingSSL() { return use_ssl; };

    void RegisterSessionHandler(std::shared_ptr<kis_httpd_websession> in_session);

    // All standard handlers require a login
    void RegisterHandler(kis_net_httpd_handler *in_handler);
    void RemoveHandler(kis_net_httpd_handler *in_handler);

    // Unauth handlers do not require a login; use of them should be very limited.
    void RegisterUnauthHandler(kis_net_httpd_handler *in_handler);
    void RemoveUnauthHandler(kis_net_httpd_handler *in_handler);

    static std::string get_suffix(std::string url);
    static std::string strip_suffix(std::string url);

    void RegisterMimeType(std::string suffix, std::string mimetype);
    std::string GetMimeType(std::string suffix);

    // Register a static files directory (used for system, home, and plugin data)
    void RegisterStaticDir(std::string in_url_prefix, std::string in_path);

    // Fixed alias/rewrites
    void RegisterAlias(const std::string& in_alias, const std::string& in_dest);
    void RemoveAlias(const std::string& in_alias);

    // Interrogate the session handler and figure out if this connection has a
    // valid session; optionally sends basic auth failure automatically
    bool HasValidSession(kis_net_httpd_connection *connection, bool send_reject = true);

    // Create a session; if connection is not null, insert session into connection.
    // If response is not null, append to the response
    std::shared_ptr<kis_net_httpd_session> CreateSession(kis_net_httpd_connection *connection, 
            struct MHD_Response *response, time_t in_lifetime);

    // Append a session cookie if we have a valid session for this connection
    static void AppendHttpSession(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection);

    // Append timestamp and mime headers
    static void AppendStandardHeaders(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection, const char *url);

    // Queue a http response
    static int SendHttpResponse(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection);

    // Send a standard HTTP response appending the session and standard 
    // headers
    static int SendStandardHttpResponse(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection, const char *url);

    // Catch MHD panics and try to close more elegantly
    static void MHD_Panic(void *cls, const char *file, unsigned int line,
            const char *reason);

protected:
    unsigned int http_port;
    std::string http_host;

    bool http_serve_files, http_serve_user_files;

    std::string uri_prefix;

    struct MHD_Daemon *microhttpd;

    // Vector of unauthorized handlers that do not need a login; there should be very very few
    // of these.  Static file handlers, and the very basic user name handler to make the initial
    // login display are about the only ones
    std::vector<kis_net_httpd_handler *> unauth_handler_vec;

    // General handler vec.  All of these require a valid login.
    std::vector<kis_net_httpd_handler *> handler_vec;

    std::string conf_username, conf_password;

    bool use_ssl;
    char *cert_pem, *cert_key;
    std::string pem_path, key_path;

    bool running;

    std::map<std::string, std::string> mime_type_map;

    std::map<std::string, std::string> alias_rewrite_map;

    class static_dir {
    public:
        static_dir(std::string prefix, std::string path) {
            this->prefix = prefix;
            this->path = path;
        };

        std::string prefix;
        std::string path;
    };

    std::vector<static_dir> static_dir_vec;

    kis_recursive_timed_mutex controller_mutex;
    kis_recursive_timed_mutex session_mutex;

    // Handle the requests and dispatch to controllers
    static int http_request_handler(void *cls, struct MHD_Connection *connection,
            const char *url, const char *method, const char *version,
            const char *upload_data, size_t *upload_data_size, void **ptr);

    static void http_request_completed(void *cls, struct MHD_Connection *connection,
            void **con_cls, enum MHD_RequestTerminationCode toe);

    static int handle_static_file(void *cls, kis_net_httpd_connection *connection,
            const char *url, const char *method);

    static int http_post_handler(void *coninfo_cls, enum MHD_ValueKind kind, 
            const char *key, const char *filename, const char *content_type,
            const char *transfer_encoding, const char *data, 
            uint64_t off, size_t size);

    char *read_ssl_file(std::string in_fname);

    void AddSession(std::shared_ptr<kis_net_httpd_session> in_session);
    void DelSession(std::string in_key);
    void DelSession(std::map<std::string, std::shared_ptr<kis_net_httpd_session>>::iterator in_itr);
    // Find a valid session; will return a session or nullptr if no session key is found, or if the
    // session is found but expired.
    std::shared_ptr<kis_net_httpd_session> FindSession(const std::string& in_session_key);
    void WriteSessions();

    std::map<std::string, std::shared_ptr<kis_net_httpd_session>> session_map;

    bool store_sessions;
    std::string sessiondb_file;
    config_file *session_db;

    std::shared_ptr<kis_httpd_websession> websession;
    unsigned int session_timeout;

};

#endif

