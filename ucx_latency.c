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
    void *remote_buf = malloc(msg_size);
    memset(local_buf, 0, msg_size);
    memset(remote_buf, 0, msg_size);

    ucp_request_param_t params;
    request_init(&params);

    double start_time = get_time();

    for (int i = 0; i < 100; ++i) {
        ucs_status_t status;

        // Put operation
        params.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK;
        params.cb.send = (ucp_send_nbx_callback_t)flush_callback;
        void *request = ucp_put_nbx(ctx->ep, local_buf, msg_size, (uintptr_t)remote_buf, &params);
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
    free(remote_buf);
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

    status = ucp_ep_create(ctx.worker, &params, &ctx.ep);
    if (status != UCS_OK) {
        fprintf(stderr, "ucp_ep_create failed\n");
        ucp_worker_release_address(ctx.worker, ctx.address);
        ucp_worker_destroy(ctx.worker);
        ucp_cleanup(ctx.context);
        return -1;
    }

    for (size_t msg_size = 8; msg_size <= MAX_MSG_SIZE; msg_size *= 2) {
        ping_pong(&ctx, msg_size);
    }

    ucp_ep_destroy(ctx.ep);
    ucp_worker_release_address(ctx.worker, ctx.address);
    ucp_worker_destroy(ctx.worker);
    ucp_cleanup(ctx.context);

    return 0;
}
