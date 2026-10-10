/*
 * Copyright (c) 2021 TiaC Systems
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "main.h"

#include <app/udp.h>
#include <app/app_api.h>
#include <hu/hupacket.h>
#include <hu/palloc.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <huerrno.h>
#include <stdio.h>

#if CONFIG_HU_APP
LOG_MODULE_REGISTER(hup_server, CONFIG_LOG_DEFAULT_LEVEL);

#if CONFIG_NET_L2_ETHERNET
struct handle;
struct udp_session {
	struct handle *owner;
	struct app_peer peer;
	void *hup;
	int64_t last_active;
};
#endif

struct handle
{
	void* hup;
	void* drv;
	const struct app_api* api;

	k_tid_t tid;
	atomic_t stopping;
	struct k_thread tdata;
	char buffer[RECV_BUFFER_SIZE];
#if CONFIG_NET_L2_ETHERNET
	/* Only datagram servers allocate storage for these session slots. */
	struct udp_session sessions[];
#endif
};

static ssize_t _send_stream(void *user_data, const uint8_t *buffer, size_t size)
{
	struct handle *h = user_data;
	return h->api->send(h->drv, buffer, size);
}

#if CONFIG_NET_L2_ETHERNET
static ssize_t _send_datagram(void *user_data, const uint8_t *buffer, size_t size)
{
	struct udp_session *session = user_data;
	struct handle *h = session->owner;
	return h->api->sendto(h->drv, buffer, size, &session->peer);
}

static void expire_udp_sessions(struct handle *h, int64_t now)
{
	for (int i = 0; i < CONFIG_HU_UDP_MAX_SESSIONS; i++) {
		struct udp_session *session = &h->sessions[i];
		if (session->hup != NULL &&
			now - session->last_active >= CONFIG_HU_UDP_SESSION_TIMEOUT_MS) {
			deinit_hupacket(session->hup);
			session->hup = NULL;
		}
	}
}

static struct udp_session *get_udp_session(struct handle *h, const struct app_peer *peer)
{
	struct udp_session *available = NULL;
	for (int i = 0; i < CONFIG_HU_UDP_MAX_SESSIONS; i++) {
		struct udp_session *session = &h->sessions[i];
		if (session->hup == NULL) {
			if (available == NULL) available = session;
		} else if (session->peer.ipv4_addr == peer->ipv4_addr &&
			session->peer.port == peer->port) {
			return session;
		}
	}
	if (available == NULL) {
		LOG_DBG("HU UDP session limit reached; dropping new sender");
		return NULL;
	}
	available->owner = h;
	available->peer = *peer;
	available->hup = init_hupacket(NULL, _send_datagram, available);
	if (available->hup == NULL) {
		LOG_ERR("Cannot allocate HU UDP parser");
		return NULL;
	}
	return available;
}
#endif

static void _hup_server(void* arg1, void* arg2, void* arg3)
{
	int received;
	struct handle* h = arg1;

	while (!atomic_get(&h->stopping))
	{
#if CONFIG_NET_L2_ETHERNET
		struct app_peer peer;
		if (h->api->recvfrom != NULL)
			received = h->api->recvfrom(h->drv, h->buffer, sizeof(h->buffer), &peer);
		else
#endif
			received = h->api->recv(h->drv, h->buffer, sizeof(h->buffer));
		if (atomic_get(&h->stopping))
			break;
		if (received < 0)
		{
			LOG_ERR("HU receive error %d", received);
			break;
		}
#if CONFIG_NET_L2_ETHERNET
		if (h->api->recvfrom != NULL) {
			expire_udp_sessions(h, k_uptime_get());
			if (received > 0) {
				struct udp_session *session = get_udp_session(h, &peer);
				if (session != NULL) {
					process_hupacket(session->hup, h->buffer, received);
					session->last_active = k_uptime_get();
				}
			}
			continue;
		}
#endif
		if (received)
		{
			process_hupacket(h->hup, h->buffer, received);
		}
	}
}

void* init_hup_server(const struct app_api* api, const char* name, void* stack, int stack_size, void* arg1, void* arg2, void* arg3)
{
	struct handle* h;
	bool datagram = false;
	size_t handle_size = sizeof(struct handle);

	if (api == NULL || api->init == NULL || api->deinit == NULL ||
		api->stop == NULL ||
		stack == NULL || stack_size <= 0)
		return NULL;

	if (api->recvfrom != NULL || api->sendto != NULL) {
#if CONFIG_NET_L2_ETHERNET
		if (api->recvfrom == NULL || api->sendto == NULL) return NULL;
		datagram = true;
		handle_size += CONFIG_HU_UDP_MAX_SESSIONS * sizeof(struct udp_session);
#else
		return NULL;
#endif
	} else if (api->recv == NULL || api->send == NULL) {
		return NULL;
	}
	h = palloc(handle_size);
	if (h == NULL)
	{
		LOG_ERR("Not enough memory");
		return NULL;
	}

	memset(h, 0, handle_size);
	h->api = api;

	h->drv = api->init(arg1, arg2, arg3);
	if (h->drv == NULL)
		goto error_drv_init_hup_server;

	if (!datagram) {
		h->hup = init_hupacket(NULL, _send_stream, h);
		if (h->hup == NULL)
			goto error_hup_init_hup_server;
	}

	h->tid = k_thread_create(&h->tdata, stack, stack_size,
		_hup_server, h, NULL, NULL,
		7, 0, K_FOREVER);
	if (name != NULL)
		k_thread_name_set(h->tid, name);
	k_thread_start(h->tid);
	return h;

error_hup_init_hup_server:
	api->stop(h->drv);
	api->deinit(h->drv);
error_drv_init_hup_server:
	pfree(h);
	return NULL;
}

void deinit_hup_server(void *handle, const struct app_api* api)
{
	struct handle* h = handle;

	if (handle == NULL)
		return;

	/* The handle owns its API. A caller-supplied API must not free it using
	 * a different transport. Lifecycle operations are serialized by caller.
	 */
	ARG_UNUSED(api);
	if (h->tid == k_current_get()) {
		LOG_ERR("Cannot destroy HU server from its own command thread");
		return;
	}
	atomic_set(&h->stopping, 1);
	h->api->stop(h->drv);

	if (h->tid != NULL)
	{
		/* Let a running command release its locks before freeing anything. */
		int rc = k_thread_join(h->tid, K_FOREVER);
		if (rc != 0) {
			LOG_ERR("Cannot join HU server: %d", rc);
			return;
		}
	}
	h->api->deinit(h->drv);

	if (h->hup != NULL)
		deinit_hupacket(h->hup);

#if CONFIG_NET_L2_ETHERNET
	if (h->api->recvfrom != NULL) {
		for (int i = 0; i < CONFIG_HU_UDP_MAX_SESSIONS; i++) {
			if (h->sessions[i].hup != NULL)
				deinit_hupacket(h->sessions[i].hup);
		}
	}
#endif
	pfree(handle);
}
#endif
