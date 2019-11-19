## Simpe JSON-RPC library

This library was built using [PJPROJECT](https://www.pjsip.org/).

## Simpe JSON-RPC server example

main.c

```
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "pj_json_rpc.h"

#define THIS_FILE "main.c"

PJ_JSON_RPC_SERVER_S *p_rpc_server = NULL;

void intHandler(int dummy) {
    p_rpc_server->quit_flag = 1;
}

void* example_thread_proc(struct thread_args *targs) {
    PJ_LOG(3,(THIS_FILE, "example_thread_proc"));
    pj_thread_desc desc;
    pj_thread_t *this_thread;
    pj_status_t rc;
    
    struct thread_args args = *targs;
    
    pj_bzero(desc, sizeof(desc));
    
    this_thread = pj_thread_this();
    if (this_thread == NULL) {
        PJ_LOG(3,(THIS_FILE, "...error: pj_thread_this() returns NULL!"));
        return NULL;
    }
    
    const char* thr_name = pj_thread_get_name(this_thread);
    if (thr_name == NULL) {
        PJ_LOG(3,(THIS_FILE, "...error: pj_thread_get_name() returns NULL!"));
        return NULL;
    }
    
    PJ_LOG(2, (THIS_FILE, " thread %s running..", thr_name));
    
    PJ_JSON_RPC_REQUEST_S request = {0};
    request.clientSocket = args.newsock;
    rc = alloc_request(args.rpc_server, &request);
    
    rc = parse_request(&request);
    if (rc != PJ_SUCCESS) {
        PJ_LOG(3,(THIS_FILE, "parse_request failed with %d", rc));
        return (void*)NULL;
    }
    rc = invoke_procedure(args.rpc_server, &request);
    if (rc != PJ_SUCCESS) {
        PJ_LOG(3,(THIS_FILE, "parse_request failed with %d", rc));
        return (void*)NULL;
    }
    
    pj_pool_release(request.pool);
    
    PJ_LOG(2, (THIS_FILE, " thread %s quitting..", thr_name));
    return (void*)NULL;
}

struct pj_json_elem say_hello(PJ_JSON_RPC_ERROR_S *error, struct pj_json_elem * params, int id) {
    pj_json_elem ret;
    pj_str_t result_name = pj_str("result");
    pj_str_t val  = pj_str("say_hello");
    pj_json_elem_string(&ret, &result_name, &val);
    return ret;
}

/*
 * 
 */
int main(int argc, char** argv) {
    pj_status_t status = EXIT_SUCCESS;
    
    // Must init PJLIB before anything else
    status = pj_init();
    if (status != PJ_SUCCESS) {
        return status;
    }
    
    if (argc != 2) {
        PJ_LOG(2, (THIS_FILE, "Few arguments to run\n\n"
                "Usage: %s PORT", argv[0])); 
        return status;
    }
    
    /*
     * First argument should be the port.
     */
    unsigned short port = (unsigned short) atoi(argv[1]);

    PJ_JSON_RPC_SERVER_S rpc_server = {0};
    
    struct sigaction a;
    a.sa_handler = intHandler;
    a.sa_flags = 0;
    sigemptyset( &a.sa_mask );
    sigaction( SIGINT, &a, NULL );
    
    p_rpc_server = &rpc_server;
    rpc_server.thread_function = (void*) example_thread_proc;
    status = create_server(&rpc_server);
    
    rpc_server.addr.sin_port = pj_htons(port);
    
    pj_rpc_register_procedure(&rpc_server, say_hello, "sayHello", NULL );
    
    status = server_loop(&rpc_server);
    status = release_server(&rpc_server);
    pj_shutdown();
    return status;
}
```