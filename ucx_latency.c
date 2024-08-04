#include <ucp/api/ucp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#define MAX_MSG_SIZE (10 * 1024 * 1024) // 10 MB

typedef struct {
    ucp_address_t *address;
    size_t address_length;
    ucp_ep_h ep;
    ucp_worker_h worker;
    ucp_context_h context;
    ucp_rkey_h rkey;
    void *remote_mem;
} ucx_context_t;

static void request_init(void *request) {
    ucp_request_param_t *params = request;
    params->op_attr_mask = 0;
}

static void flush_callback(void *request, ucs_status_t status) {
    if (status != UCS_OK) {
        fprintf(stderr, "flush failed with status %d\n", status);
    }
}

static void wait(ucp_worker_h worker, void *request) {
    ucs_status_t status;
    while ((status = ucp_request_check_status(request)) == UCS_INPROGRESS) {
        ucp_worker_progress(worker);
    }
    ucp_request_release(request);
}

static double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void ping_pong(ucx_context_t *ctx, size_t msg_size) {
    void *local_buf = malloc(msg_size);
    memset(local_buf, 0, msg_size);

    ucp_request_param_t params;
    request_init(&params);

    double start_time = get_time();

    for (int i = 0; i < 100; ++i) {
        ucs_status_t status;

        // Put operation
        void *request = ucp_put_nbx(ctx->ep, local_buf, msg_size, (uintptr_t)ctx->remote_mem, ctx->rkey, &params);
        if (UCS_PTR_IS_ERR(request)) {
            fprintf(stderr, "ucp_put_nbx failed\n");
            exit(1);
        }
        if (request != NULL) {
            wait(ctx->worker, request);
        }

        // Flush operation
        request = ucp_worker_flush_nbx(ctx->worker, &params);
        if (UCS_PTR_IS_ERR(request)) {
            fprintf(stderr, "ucp_worker_flush_nbx failed\n");
            exit(1);
        }
        if (request != NULL) {
            wait(ctx->worker, request);
        }
    }

    double end_time = get_time();
    double latency = (end_time - start_time) / 100 * 1000; // milliseconds
    printf("Message size: %zu bytes, latency: %.2f ms\n", msg_size, latency);

    free(local_buf);
}

int main(int argc, char **argv) {
    ucp_params_t ucp_params;
    ucp_worker_params_t worker_params;
    ucp_config_t *config;
    ucx_context_t ctx;

    ucs_status_t status = ucp_config_read(NULL, NULL, &config);
    if (status != UCS_OK) {
        fprintf(stderr, "ucp_config_read failed\n");
        return -1;
    }

    ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES;
    ucp_params.features = UCP_FEATURE_RMA;

    status = ucp_init(&ucp_params, config, &ctx.context);
    ucp_config_release(config);
    if (status != UCS_OK) {
        fprintf(stderr, "ucp_init failed\n");
        return -1;
    }

    worker_params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;

    status = ucp_worker_create(ctx.context, &worker_params, &ctx.worker);
    if (status != UCS_OK) {
        fprintf(stderr, "ucp_worker_create failed\n");
        ucp_cleanup(ctx.context);
        return -1;
    }

    status = ucp_worker_get_address(ctx.worker, &ctx.address, &ctx.address_length);
    if (status != UCS_OK) {
        fprintf(stderr, "ucp_worker_get_address failed\n");
        ucp_worker_destroy(ctx.worker);
        ucp_cleanup(ctx.context);
        return -1;
    }

    // Exchange addresses between Rank 0 and Rank 1
    // ...

    ucp_ep_params_t ep_params;
    ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
    ep_params.address = ctx.address;

    status = ucp_ep_create(ctx.worker, &ep_params, &ctx.ep);
    if (status != UCS_OK) {
        fprintf(stderr, "ucp_ep_create failed\n");
        ucp_worker_release_address(ctx.worker, ctx.address);
        ucp_worker_destroy(ctx.worker);
        ucp_cleanup(ctx.context);
        return -1;
    }

    // Allocate remote memory and register the key
    ctx.remote_mem = malloc(MAX_MSG_SIZE);
    ucp_mem_map_params_t mem_map_params;
    mem_map_params.field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
                                UCP_MEM_MAP_PARAM_FIELD_LENGTH;
    mem_map_params.address = ctx.remote_mem;
    mem_map_params.length = MAX_MSG_SIZE;

    ucp_mem_h memh;
    status = ucp_mem_map(ctx.context, &mem_map_params, &memh);
    if (status != UCS_OK) {
        fprintf(stderr, "ucp_mem_map failed\n");
        ucp_ep_destroy(ctx.ep);
        ucp_worker_release_address(ctx.worker, ctx.address);
        ucp_worker_destroy(ctx.worker);
        ucp_cleanup(ctx.context);
        return -1;
    }

    ucp_rkey_bundle_t rkey_bundle;
    status = ucp_rkey_pack(ctx.context, memh, &rkey_bundle);
    if (status != UCS_OK) {
        fprintf(stderr, "ucp_rkey_pack failed\n");
        ucp_mem_unmap(ctx.context, memh);
        ucp_ep_destroy(ctx.ep);
        ucp_worker_release_address(ctx.worker, ctx.address);
        ucp_worker_destroy(ctx.worker);
        ucp_cleanup(ctx.context);
        return -1;
    }

    status = ucp_ep_rkey_unpack(ctx.ep, rkey_bundle.rkey_buffer, &ctx.rkey);
    if (status != UCS_OK) {
        fprintf(stderr, "ucp_ep_rkey_unpack failed\n");
        ucp_rkey_buffer_release(rkey_bundle.rkey_buffer);
        ucp_mem_unmap(ctx.context, memh);
        ucp_ep_destroy(ctx.ep);
        ucp_worker_release_address(ctx.worker, ctx.address);
        ucp_worker_destroy(ctx.worker);
        ucp_cleanup(ctx.context);
        return -1;
    }

    ucp_rkey_buffer_release(rkey_bundle.rkey_buffer);

    for (size_t msg_size = 8; msg_size <= MAX_MSG_SIZE; msg_size *= 2) {
        ping_pong(&ctx, msg_size);
    }

    ucp_rkey_destroy(ctx.rkey);
    ucp_mem_unmap(ctx.context, memh);
    ucp_ep_destroy(ctx.ep);
    ucp_worker_release_address(ctx.worker, ctx.address);
    ucp_worker_destroy(ctx.worker);
    ucp_cleanup(ctx.context);

    return 0;
}
