/* 
 * File:   pj_rpc.h
 * Author: ussh
 *
 * Created on 6 июля 2019 г., 14:14
 */

#ifndef PJ_JSON_RPC_H
#define PJ_JSON_RPC_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "pjlib.h"
#include <pjlib-util/json.h>
#include <pj/log.h>
#include <pj/string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIB_FILE "JSON_RPC_LIB"
    
#define PJ_JSON_RPC_PARSE_ERROR      -32700
#define PJ_JSON_RPC_INVALID_REQUEST  -32600
#define PJ_JSON_RPC_METHOD_NOT_FOUND -32601
#define PJ_JSON_RPC_INVALID_PARAMS   -32603
#define PJ_JSON_RPC_INTERNAL_ERROR   -32693
#define PJ_JSON_RPC_UNKNOWN_ERROR    -32000
    
#define MAX_BUFF_SIZE 2000

typedef struct {
    void *data;
    int error_code;
    char * error_message;
} pj_json_rpc_context;

typedef struct pj_json_RPC_ERROR {
    int code;
    pj_str_t *message;
    void     *data;
} PJ_JSON_RPC_ERROR_S;

typedef struct pj_json_elem (*pj_json_rpc_function)(PJ_JSON_RPC_ERROR_S *error, struct pj_json_elem *params, int id);
typedef void* (*pj_json_rpc_thread_function)(void *targs);

typedef struct pj_rpc_procedure {
    pj_str_t name;
    pj_json_rpc_function function;
    void *data;
} PJ_JSON_RPC_PROC_S;

typedef struct pj_RPC_SERVER {
    pj_pool_t *pool;
    pj_caching_pool cp;
    pj_sock_t serverSocket;
    pj_sockaddr_in addr;
    int quit_flag;
    int proc_count;
    PJ_JSON_RPC_PROC_S *proc_array;
    pj_json_rpc_thread_function thread_function;
} PJ_JSON_RPC_SERVER_S;

typedef struct pj_RPC_RESULT {
    int code;
    pj_str_t message;
} PJ_JSON_RPC_RESULT_S;

typedef struct pj_RPC_REQUEST {
    int id;
    pj_sock_t clientSocket;
    pj_str_t *proc_name;
    struct pj_json_elem *proc_params;
    pj_pool_t *pool;
} PJ_JSON_RPC_REQUEST_S;
        
typedef struct pj_RPC_RESPONSE {
    int id;
    pj_sock_t clientSocket;
    pj_str_t *proc_name;
    struct pj_json_elem *proc_params;
} PJ_JSON_RPC_RESPONSE_S;

struct thread_args {
    pj_sock_t newsock;
    PJ_JSON_RPC_SERVER_S *rpc_server;
};

static int send_json(PJ_JSON_RPC_REQUEST_S *request, struct pj_json_elem* root) {
    char client_message[MAX_BUFF_SIZE];
    unsigned sz = MAX_BUFF_SIZE;
    pj_status_t rc = PJ_SUCCESS;

    pj_json_write(root, client_message, &sz);
    PJ_LOG(2, (LIB_FILE, "client_message: %s, sz = %u", client_message, sz));

    rc = pj_sock_send(request->clientSocket, client_message, (pj_ssize_t *)&sz, 0);
    if (rc != PJ_SUCCESS) {
        PJ_LOG(3,(LIB_FILE, "pj_sock_send failed with %d", rc));
    }
    
    rc = pj_sock_close(request->clientSocket);
    if (rc != PJ_SUCCESS) {
        PJ_LOG(2, (LIB_FILE, "pj_sock_close failed with %d", rc));
    } else {
        PJ_LOG(2, (LIB_FILE, "pj_sock_close done"));
    };
    return rc;
}

static int prepare_answer(pj_json_elem *root) {
    pj_json_elem_obj(root, NULL);
    pj_json_elem version;
    pj_str_t version_name = pj_str("jsonrpc");
    pj_str_t version_val  = pj_str("2.0");
    pj_json_elem_string(&version, &version_name, &version_val);
    pj_json_elem_add(root, &version);
    return PJ_SUCCESS;
}

static int send_response(PJ_JSON_RPC_REQUEST_S *request, struct pj_json_elem* result) {
    pj_status_t rc = PJ_SUCCESS;
   
    pj_json_elem root;
    prepare_answer(&root);
    
    pj_json_elem id_elem;
    pj_str_t id_elem_name = pj_str("id");
    pj_json_elem_number(&id_elem, &id_elem_name, request->id);
    
    pj_json_elem_add(&root, result);
    pj_json_elem_add(&root, &id_elem);
    
    rc = send_json(request, &root);
    return rc;
}

static int send_error(PJ_JSON_RPC_REQUEST_S *request, PJ_JSON_RPC_ERROR_S error, int id) {
    pj_status_t rc = PJ_SUCCESS;
   
    pj_json_elem root;
    prepare_answer(&root);
    
    pj_json_elem code;
    pj_str_t code_name = pj_str("code");
    pj_json_elem_number(&code, &code_name, error.code);
    pj_json_elem message;
    pj_str_t message_name = pj_str("message");
    pj_json_elem_string(&message, &message_name, error.message);
    
    pj_json_elem result;
    pj_str_t result_name  = pj_str("error");
    pj_json_elem_obj(&result, &result_name);
    
    pj_json_elem_add(&result, &code);
    pj_json_elem_add(&result, &message);

    pj_json_elem id_elem;
    pj_str_t id_elem_name = pj_str("id");
    pj_json_elem_number(&id_elem, &id_elem_name, id);
    pj_json_elem_add(&root, &result);
    pj_json_elem_add(&root, &id_elem);

    rc = send_json(request, &root);
    return rc;
}

int pj_rpc_register_procedure(
    PJ_JSON_RPC_SERVER_S *server, pj_json_rpc_function function_pointer, char *name, void * data) {
    pj_str_t pj_str_name = pj_str(name);
    int i = server->proc_count++;
    if (!server->proc_array)
        server->proc_array = malloc(sizeof(PJ_JSON_RPC_PROC_S));
    else {
        PJ_JSON_RPC_PROC_S * ptr = realloc(server->proc_array,
                sizeof(PJ_JSON_RPC_PROC_S) * server->proc_count);
        if (!ptr)
                return -1;
        server->proc_array = ptr;

    }
    server->proc_array[i].name = pj_str_name;
    server->proc_array[i].function = function_pointer;
    server->proc_array[i].data = data;
    return 0;
}

int invoke_procedure(PJ_JSON_RPC_SERVER_S *server, PJ_JSON_RPC_REQUEST_S *request) {
    struct pj_json_elem returned = {0};
    int procedure_found = 0;
    PJ_JSON_RPC_ERROR_S error = {0};
    
    error.code = 0;
    error.message = NULL;
    int i = server->proc_count;
    while (i--) {
        if (pj_strcmp(&server->proc_array[i].name, request->proc_name) == 0) {
            procedure_found = 1;
            error.data = server->proc_array[i].data;
            returned = server->proc_array[i].function(&error, request->proc_params, request->id);
            break;
        }
    }
    if (!procedure_found) {
        error.code = PJ_JSON_RPC_METHOD_NOT_FOUND;
        pj_str_t mess = pj_str("Method not found.");
        error.message = (pj_str_t*)&mess;
        error.data = NULL;
        return send_error(request, error, request->id);
    } else {
        if (error.code)
            return send_error(request, error, request->id);
        else
            return send_response(request, &returned);
    }
}

int parse_request(PJ_JSON_RPC_REQUEST_S *request);

pj_json_elem *find_value_by_name(pj_json_elem* root_value, char *value_name) {
    if (root_value == NULL) {
        return NULL;
    }
    pj_str_t t_name = pj_str(value_name);
    
    struct pj_json_elem *tmp = (struct pj_json_elem *)root_value->value.children.next;
    int obj_count = 4;
    while (obj_count--) {
        if (pj_strcmp(&tmp->name, &t_name) == 0) {
            return tmp;
        } else {
            if (tmp->next == NULL) {
                return NULL;
            }
            tmp = (struct pj_json_elem *)tmp->next;
        };
    }
    return NULL;
}

int alloc_request(PJ_JSON_RPC_SERVER_S *server, PJ_JSON_RPC_REQUEST_S *request) {
    pj_status_t rc;
    char pool_name[255];
    PJ_LOG(2, (LIB_FILE, "Start creating pool")); 
    pj_ansi_sprintf(pool_name, "request_pool%ld", request->clientSocket);
    request->pool = pj_pool_create(server->pool->factory,
        pool_name, // pool's name
        4000, // initial size
        4000, // increment size
        NULL
    );
    if (request->pool == NULL) {
        PJ_LOG(2, (LIB_FILE, "Error creating pool"));
        return PJ_ENOMEM;
    } else {
        PJ_LOG(2, (LIB_FILE, "Creating pool done"));
    }
    return PJ_SUCCESS;
}

void* thread_proc(struct thread_args *targs) {
    PJ_LOG(3,(LIB_FILE, "example_thread_proc"));
    pj_thread_desc desc;
    pj_thread_t *this_thread;
    pj_status_t rc;
    
    struct thread_args args = *targs;
    
    pj_bzero(desc, sizeof(desc));
    
    this_thread = pj_thread_this();
    if (this_thread == NULL) {
        PJ_LOG(3,(LIB_FILE, "...error: pj_thread_this() returns NULL!"));
        return NULL;
    }
    
    const char* thr_name = pj_thread_get_name(this_thread);
    if (thr_name == NULL) {
        PJ_LOG(3,(LIB_FILE, "...error: pj_thread_get_name() returns NULL!"));
        return NULL;
    }
    
    PJ_LOG(2, (LIB_FILE, " thread %s running..", thr_name));
    
    PJ_JSON_RPC_REQUEST_S request = {0};
    request.clientSocket = args.newsock;
    rc = alloc_request(args.rpc_server, &request);
    
    rc = parse_request(&request);
    if (rc != PJ_SUCCESS) {
        PJ_LOG(3,(LIB_FILE, "parse_request failed with %d", rc));
        return (void*)NULL;
    }
    rc = invoke_procedure(args.rpc_server, &request);
    if (rc != PJ_SUCCESS) {
        PJ_LOG(3,(LIB_FILE, "parse_request failed with %d", rc));
        return (void*)NULL;
    }
    
    pj_pool_release(request.pool);
    
    PJ_LOG(2, (LIB_FILE, " thread %s quitting..", thr_name));
    return (void*)NULL;
}

int create_server(PJ_JSON_RPC_SERVER_S *rpc_server) {
    struct sockaddr_storage serverStorage;
    pj_status_t rc = PJ_SUCCESS;
    
    pj_caching_pool_init(&rpc_server->cp, NULL, 1024*1024 );
    
    rpc_server->pool = pj_pool_create(&rpc_server->cp.factory,
        "pool1", // pool's name
        4000, // initial size
        4000, // increment size
        NULL
    );
    if (rpc_server->pool == NULL) {
        PJ_LOG(2, (LIB_FILE, "Error creating pool"));
        return PJ_ENOMEM;
    }
    
    rpc_server->quit_flag = 0;
    pj_bzero(&rpc_server->addr, sizeof(rpc_server->addr));
    rpc_server->addr.sin_family = pj_AF_INET();
    rpc_server->addr.sin_addr.s_addr  = htonl(INADDR_ANY);
    
    memset(rpc_server->addr.sin_zero, '\0', sizeof rpc_server->addr.sin_zero);
    
    rc = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, &rpc_server->serverSocket);
    if (rc != PJ_SUCCESS){
        PJ_LOG(2, (LIB_FILE, "...pj_sock_socket failed with %d", rc)); 
        return rc;
    }
    
    return PJ_SUCCESS;
}

int release_server(PJ_JSON_RPC_SERVER_S *rpc_server) {
    pj_status_t rc = 0;
    
    rc = pj_sock_shutdown(rpc_server->serverSocket, PJ_SHUT_RDWR);
    if (rc < 0) {
        PJ_LOG(2, (LIB_FILE, "Shutdown server socket failed with %d..", rc));
    } else {
        PJ_LOG(2, (LIB_FILE, "Shutdown server socket done"));
    };
    PJ_LOG(2, (LIB_FILE, "Deinit pool.."));
    pj_pool_release(rpc_server->pool);
    PJ_LOG(2, (LIB_FILE, "Deinit caching pool.."));
    pj_caching_pool_destroy(&rpc_server->cp);
    
    return PJ_SUCCESS;
}

int server_loop(PJ_JSON_RPC_SERVER_S *rpc_server) {
    pj_status_t rc = PJ_SUCCESS;
    socklen_t addr_size;
    pj_thread_t *thread;
    
    rc = pj_sock_bind(rpc_server->serverSocket, &rpc_server->addr, sizeof(rpc_server->addr));
    if (rc != PJ_SUCCESS){
        PJ_LOG(2, (LIB_FILE, "...pj_sock_bind failed with %d", rc)); 
        return rc;
    } else {
        PJ_LOG(2, (LIB_FILE, "Socket successfully binded..")); 
    }
    
#if PJ_HAS_TCP
    rc = pj_sock_listen(rpc_server->serverSocket, PJ_SOMAXCONN);
    if (rc != PJ_SUCCESS) {
        PJ_LOG(2, (LIB_FILE, "...pj_sock_listen failed with %d", rc)); 
        return rc;
    } else {
        PJ_LOG(2, (LIB_FILE, "Server listening.."));
    }
    
    rpc_server->quit_flag = 0;
    
    while (!rpc_server->quit_flag) {
        pj_sock_t newsock;
        pj_sockaddr_in sStorage;
        int l = 0;
        addr_size = sizeof sStorage;
        struct sockaddr_in sin = {0};
        rc = pj_sock_accept(rpc_server->serverSocket, &newsock, &sStorage, &addr_size);
        if (rc != PJ_SUCCESS) {
            PJ_LOG(2, (LIB_FILE, "...accept failed with %d", rc)); 
            return rc;
        } else {
            sin.sin_addr.s_addr = sStorage.sin_addr.s_addr;
            sin.sin_family = sStorage.sin_family;
            sin.sin_port = sStorage.sin_port;
            sin.sin_zero[0] = sStorage.sin_zero[0];
            
            char thr_name[255];
            pj_ansi_sprintf(thr_name, "%s_%08x", (char*)inet_ntoa((struct in_addr)sin.sin_addr), thread);
            
            PJ_LOG(2, (LIB_FILE, "Server acccept the client... %s", thr_name));
            struct thread_args targs = {0};
            targs.rpc_server = (void*)rpc_server;
            targs.newsock = newsock;
            if (!rpc_server->thread_function) {
                rpc_server->thread_function = (void*) thread_proc;
            }
            
             
            rc = pj_thread_create(
                rpc_server->pool, 
                thr_name, 
                (pj_thread_proc*)rpc_server->thread_function,
                &targs, // struct thread_args
                PJ_THREAD_DEFAULT_STACK_SIZE,
                PJ_THREAD_SUSPENDED, //flags,
                &thread
            );
            if (rc != PJ_SUCCESS) {
                return rc;
            }
            rc = pj_thread_resume(thread);
            if (rc != PJ_SUCCESS) {
                return rc;
            }
        }
    }
    rpc_server->quit_flag = 1;    
#endif

    rc = pj_sock_close(rpc_server->serverSocket);
    if (rc != PJ_SUCCESS) {
        PJ_LOG(2, (LIB_FILE, "...error in closing socket %d", rc)); 
        return -2010;
    }
}


int parse_request(PJ_JSON_RPC_REQUEST_S *request) {
    pj_status_t rc;
    
    char client_message[MAX_BUFF_SIZE];
    unsigned sz = MAX_BUFF_SIZE;
    pj_ssize_t len = MAX_BUFF_SIZE;
    rc = pj_sock_recv(request->clientSocket, (void*)client_message, &len, 0);
    if (rc != PJ_SUCCESS) {
        PJ_LOG(3,(LIB_FILE, "pj_sock_recv failed with %d", rc));
        return rc;
    }
    
    PJ_LOG(2, (LIB_FILE, "Client message(%d, %s): %s", len, strerror(errno), client_message));
    
    sz = (unsigned) len;
    pj_json_err_info json_error;
    
    pj_json_elem *client_request = pj_json_parse(request->pool,
            (char *)&client_message, &sz, &json_error);
    
    if (!client_request) {
        PJ_LOG(1, (LIB_FILE, "Parse error. Invalid JSON was received by the server."));
        return PJ_JSON_RPC_PARSE_ERROR;
    }
    
    pj_json_elem * tmp_jsn_val = find_value_by_name(client_request, "method");
    
    if (!find_value_by_name(client_request, "jsonrpc")) {
        PJ_LOG(1, (LIB_FILE, "The JSON sent is not a valid Request object (\"jsonrpc\" not found)."));
        return PJ_JSON_RPC_INVALID_REQUEST;
    } else {
        pj_json_elem * tmp_mthd_val = find_value_by_name(client_request, "method");
        if (!tmp_mthd_val) {
            PJ_LOG(1, (LIB_FILE, "The JSON sent is not a valid Request object (\"method\" not found)."));
            return PJ_JSON_RPC_INVALID_REQUEST;
        } else {
            pj_json_elem * tmp_id_val = find_value_by_name(client_request, "id");
            if (!tmp_id_val) {
                PJ_LOG(1, (LIB_FILE, "The JSON sent is not a valid Request object (\"id\" not found)."));
                return PJ_JSON_RPC_INVALID_REQUEST;
            } else {
                pj_json_elem * tmp_prms_val = find_value_by_name(client_request, "params");
                if (tmp_prms_val) {
                    request->proc_params = tmp_prms_val;
                } else {
                    PJ_LOG(1, (LIB_FILE, "params not found."));
                    request->proc_params = NULL;
                }
                
                request->id = (int)tmp_id_val->value.num;
                request->proc_name = (pj_str_t *)&tmp_mthd_val->value.str;
                return PJ_SUCCESS;
            }
        }
    }
    
    PJ_LOG(1, (LIB_FILE, "UNKNOWN ERROR."));
    return PJ_JSON_RPC_UNKNOWN_ERROR;
}

#ifdef __cplusplus
}
#endif

#endif /* PJ_JSON_RPC_H */

