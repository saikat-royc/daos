/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This is a simple example of crt_echo rpc server header file.
 */

#ifndef __CRT_ECHO_SRV_H__
#define __CRT_ECHO_SRV_H__

#include <crt_echo.h>

struct gecho gecho;

struct echo_serv {
	int		do_shutdown;
	pthread_t	progress_thread;
} echo_srv;

static void *progress_handler(void *arg)
{
	int rc, loop = 0, i;

	assert(arg == NULL);
	/* progress loop */
	do {
		rc = crt_progress(gecho.crt_ctx, 1, NULL, NULL);
		if (rc != 0 && rc != -CER_TIMEDOUT) {
			C_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}

		if (ECHO_EXTRA_CONTEXT_NUM > 0) {
			for (i = 0; i < ECHO_EXTRA_CONTEXT_NUM; i++) {
				rc = crt_progress(gecho.extra_ctx[i], 1, NULL,
						  NULL);
				if (rc != 0 && rc != -CER_TIMEDOUT) {
					C_ERROR("crt_progress failed rc: %d.\n",
					       rc);
					break;
				}
			}
		}

		if (echo_srv.do_shutdown != 0) {
			/* to ensure the last SHUTDOWN request be handled */
			loop++;
			if (loop >= 100)
				break;
		}
	} while (1);

	printf("progress_handler: rc: %d, echo_srv.do_shutdown: %d.\n",
	       rc, echo_srv.do_shutdown);
	printf("progress_handler: progress thread exit ...\n");

	pthread_exit(NULL);
}

crt_group_t *example_grp_hdl;
int grp_create_cb(crt_group_t *grp, void *priv, int status)
{
	printf("in grp_create_cb, grp %p, priv %p, status %d.\n",
		grp, priv, status);
	example_grp_hdl = grp;
	return 0;
}

int grp_destroy_cb(void *arg, int status)
{
	printf("in grp_destroy_cb, arg %p, status %d.\n", arg, status);
	return 0;
}

int echo_srv_corpc_example(crt_rpc_t *rpc_req)
{
	struct crt_echo_corpc_example_req *req;
	struct crt_echo_corpc_example_reply *reply;
	crt_rank_t my_rank;
	int rc = 0;

	req = crt_req_get(rpc_req);
	reply = crt_reply_get(rpc_req);
	C_ASSERT(req != NULL && reply != NULL);

	crt_group_rank(NULL, &my_rank);
	reply->co_result = my_rank;

	rc = crt_reply_send(rpc_req);

	printf("echo_srv_corpc_example, rank %d got msg %s, reply %d, rc %d.\n",
	       my_rank, req->co_msg, reply->co_result, rc);

	return rc;
}

int corpc_example_aggregate(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct crt_echo_corpc_example_reply *reply_source, *reply_result;
	crt_rank_t my_rank;

	C_ASSERT(source != NULL && result != NULL);
	reply_source = crt_reply_get(source);
	reply_result = crt_reply_get(result);
	reply_result->co_result += reply_source->co_result;

	crt_group_rank(NULL, &my_rank);
	printf("corpc_example_aggregate, rank %d, co_result %d, "
	       "aggregate result %d.\n",
	       my_rank, reply_source->co_result, reply_result->co_result);

	return 0;
}

struct crt_corpc_ops echo_co_ops = {
	.co_aggregate = corpc_example_aggregate,
};

int bulk_test_cb(const struct crt_bulk_cb_info *cb_info)
{
	crt_rpc_t			*rpc_req;
	struct crt_bulk_desc		*bulk_desc;
	crt_bulk_t			local_bulk_hdl;
	crt_iov_t			*iovs;
	struct crt_echo_bulk_out_reply	*e_reply;
	struct crt_echo_bulk_in_req	*e_req;
	int				rc = 0;

	rc = cb_info->bci_rc;
	bulk_desc = cb_info->bci_bulk_desc;
	/* printf("in bulk_test_cb, dci_rc: %d.\n", rc); */
	rpc_req = bulk_desc->bd_rpc;
	iovs = (crt_iov_t *)cb_info->bci_arg;
	assert(rpc_req != NULL && iovs != NULL);

	local_bulk_hdl = bulk_desc->bd_local_hdl;
	assert(local_bulk_hdl != NULL);

	e_reply = crt_reply_get(rpc_req);
	C_ASSERT(e_reply != NULL);
	if (rc != 0) {
		printf("bulk transferring failed, dci_rc: %d.\n", rc);
		e_reply->ret = rc;
		e_reply->echo_msg = "bulk failed with data corruption.";
		goto out;
	}
	/* calculate md5 checksum to verify data */
	MD5_CTX md5_ctx;
	unsigned char md5[16];
	crt_string_t md5_str = (crt_string_t)malloc(33);

	memset(md5_str, 0, 33);

	rc = MD5_Init(&md5_ctx);
	assert(rc == 1);
	rc = MD5_Update(&md5_ctx, iovs[0].iov_buf, iovs[0].iov_buf_len);
	assert(rc == 1);
	rc = MD5_Final(md5, &md5_ctx);
	assert(rc == 1);
	echo_md5_to_string(md5, md5_str);

	e_req = crt_req_get(rpc_req);
	C_ASSERT(e_req != NULL);
	rc = strcmp(md5_str, e_req->bulk_md5_ptr);
	if (rc == 0) {
		printf("data verification success, md5: %s.\n", md5_str);
		e_reply->ret = 0;
		e_reply->echo_msg = "bulk succeed (data verified).";
	} else {
		printf("data verification failed, md5: %s, origin_md5: %s.\n",
		       md5_str, e_req->bulk_md5_ptr);
		e_reply->ret = rc;
		e_reply->echo_msg = "bulk failed with data corruption.";
	}

	free(md5_str);

out:
	free(iovs[0].iov_buf);
	free(iovs);

	rc = crt_bulk_free(local_bulk_hdl);
	assert(rc == 0);

	/*
	 * need to call crt_reply_send first and then call crt_req_decref,
	 * if changing the sequence possibly cause the RPC request be destroyed
	 * before sending reply.
	 */
	rc = crt_reply_send(rpc_req);
	assert(rc == 0);

	printf("echo_srver sent bulk_test reply, echo_msg: %s.\n",
	       e_reply->echo_msg);

	rc = crt_req_decref(rpc_req);
	assert(rc == 0);

	return 0;
}

int echo_srv_bulk_test(crt_rpc_t *rpc_req)
{
	crt_bulk_t			local_bulk_hdl;
	crt_sg_list_t			sgl;
	crt_iov_t			*iovs = NULL;
	crt_size_t			bulk_len;
	unsigned int			bulk_sgnum;
	struct crt_bulk_desc		bulk_desc;
	struct crt_echo_bulk_in_req	*e_req;
	int				rc = 0;

	e_req = crt_req_get(rpc_req);
	C_ASSERT(e_req != NULL);
	rc = crt_bulk_get_len(e_req->remote_bulk_hdl, &bulk_len);
	assert(rc == 0);
	rc = crt_bulk_get_sgnum(e_req->remote_bulk_hdl, &bulk_sgnum);

	printf("echo_srver recv'd bulk_test, opc: 0x%x, intro_msg: %s, "
	       "bulk_len: %ld, bulk_sgnum: %d.\n",
	       rpc_req->cr_opc, e_req->bulk_intro_msg, bulk_len, bulk_sgnum);

	iovs = (crt_iov_t *)malloc(sizeof(crt_iov_t));
	iovs[0].iov_buf = malloc(bulk_len);
	iovs[0].iov_buf_len = bulk_len;
	memset(iovs[0].iov_buf, 0, iovs[0].iov_buf_len);
	sgl.sg_nr.num = 1;
	sgl.sg_iovs = iovs;

	rc = crt_bulk_create(rpc_req->cr_ctx, &sgl, CRT_BULK_RW,
			     &local_bulk_hdl);
	assert(rc == 0);

	rc = crt_req_addref(rpc_req);
	assert(rc == 0);

	bulk_desc.bd_rpc = rpc_req;

	bulk_desc.bd_bulk_op = CRT_BULK_GET;
	bulk_desc.bd_remote_hdl = e_req->remote_bulk_hdl;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = local_bulk_hdl;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = bulk_len;

	/* user needs to register the complete_cb inside which can do:
	 * 1) resource reclaim includes freeing the:
	 *    a) the buffers for bulk, maybe also the crt_iov_t (iovs)
	 *    b) local bulk handle
	 *    c) cbinfo if needed,
	 * 2) reply to original RPC request (if the bulk is derived from a RPC)
	 * 3) crt_req_decref (before return in this RPC handler, need to take a
	 *    reference to avoid the RPC request be destroyed by CRT, then need
	 *    to release the reference at bulk's complete_cb);
	 */
	rc = crt_bulk_transfer(&bulk_desc, bulk_test_cb, iovs, NULL);
	assert(rc == 0);

	return rc;
}

#endif /* __CRT_ECHO_SRV_H__ */
