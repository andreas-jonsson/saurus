/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "saurus.h"

#include <string.h>
#include <ctype.h>

#ifndef SU_OPT_NO_SOCKET

#if defined(_WIN32)
	#include <Winsock2.h>
	volatile static int wsa_initialized = 0;
	static void winsock_shutdown() {
		WSACleanup();
	}
#else
	#include <netdb.h>
	#include <resolv.h>
	#include <unistd.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
#endif

#define DEFAULT_HOST "localhost"

static void extract_host_and_path(su_state *s, int idx) {
	const char *tmp, *end;
	const char *url = su_tostring(s, idx, NULL);
	
	if (*url == '/') {
		su_pushstring(s, DEFAULT_HOST);
		end = strstr(url, "?");
		if (end)
			su_pushbytes(s, url, end - url);
		else
			su_pushstring(s, url);
		return;
	}
	
	tmp = strstr(url, "://");
	if (tmp)
		url = tmp + 3;
	
	tmp = strstr(url, "/");
	if (tmp) {
		su_pushbytes(s, url, tmp - url);
		end = strstr(tmp, "?");
		if (end)
			su_pushbytes(s, tmp, end - tmp);
		else
			su_pushstring(s, tmp);
		return;
	}
		
	su_pushstring(s, url);
	su_pushstring(s, "/");
}

static void ensure_required_parameters(su_state *s, int idx, const char *host, unsigned length) {
	su_pushstring(s, "Host");
	if (!su_map_has(s, idx - 1)) {
		su_pushstring(s, "Host");
		su_pushstring(s, host);
		su_map_insert(s, idx - 2);
		su_swap(s, idx - 1, -1);
		su_pop(s, 1);
	}
	su_pushstring(s, "Content-Length");
	if (!su_map_has(s, idx - 1)) {
		su_pushstring(s, "Content-Length");
		su_pushnumber(s, (double)length);
		su_map_insert(s, idx - 2);
		su_swap(s, idx - 1, -1);
		su_pop(s, 1);
	}
	su_pushstring(s, "Connection");
	if (!su_map_has(s, idx - 1)) {
		su_pushstring(s, "Connection");
		su_pushstring(s, "close");
		su_map_insert(s, idx - 2);
		su_swap(s, idx - 1, -1);
		su_pop(s, 1);
	}
}

static const char *skip_space(const char *str) {
	while (isspace(*str))
		str++;
	return str;
}

static const char *skip_space_el(const char *str, int *empty) {
	*empty = 0;
	while (isspace(*str)) {
		if (*str == '\n')
			(*empty)++;
		str++;
	}
	return str;
}

static const char *read_string_until_nl(su_state *s, const char *str) {
	su_string_begin(s, NULL);
	while (*str && *str != '\n') {
		if (*str != '\r' && *str != '\t')
			su_string_ch(s, *str);
		str++;
	}
	su_string_push(s);
	return str;
}

static const char *read_string_until_space(su_state *s, const char *str) {
	su_string_begin(s, NULL);
	while (!isspace(*str)) {
		su_string_ch(s, *str);
		str++;
	}
	su_string_push(s);
	return str;
}

static const char *read_integer(su_state *s, const char *str) {
	int num;
	str = read_string_until_space(s, str);
	num = atoi(su_tostring(s, -1, NULL));
	su_pop(s, 1);
	su_pushinteger(s, num);
	return str;
}

static const char *parse_parameters(su_state *s, const char *str) {
	int empty_lines, count;
	unsigned size;
	const char *param;
	
	count = 0;
	do {
		str = skip_space_el(str, &empty_lines);
		if (empty_lines > 1)
			break;
		
		str = read_string_until_space(s, str);
		param = su_tostring(s, -1, &size);
		if (size) {
			su_pushbytes(s, param, size - 1);
			su_swap(s, -2, -1);
			su_pop(s, 1);
		}
	
		str = skip_space(str);
		str = read_string_until_nl(s, str);
		count++;
	} while (*str);
	
	su_map(s, count);
	return str;
}

static void parse_response(su_state *s) {
	unsigned size;
	const char *str;
	const char *response = su_tostring(s, -1, &size);
	
	if (size) {
		str = skip_space(response);
	
		str = read_string_until_space(s, str); /* Protocol */
	
		str = skip_space(str);
		str = read_integer(s, str); /* Response code */
	
		str = skip_space(str);
		str = read_string_until_nl(s, str); /* Response text */
	
		str = skip_space(str);
		str = parse_parameters(s, str); /* Parameters */
	
		su_pushbytes(s, str, size - (str - response)); /* Data */
		su_vector(s, 5);
	} else {
		su_pushnil(s);
	}
}

static int request(su_state *s, int narg) {
	char *tmp;
	int sock, i;
	unsigned size;
	struct hostent *record;
	struct sockaddr_in addr;
	struct in_addr **addr_list;
	const char *method, *path, *msg, *host;
	
	su_check_arguments(s, 4, SU_STRING, SU_STRING, SU_MAP, SU_NIL);
	
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return 0;
	
	method = su_tostring(s, -4, NULL);
	msg = su_tostring(s, -1, &size);
	extract_host_and_path(s, -3);
	path = su_tostring(s, -1, NULL);
	host = su_tostring(s, -2, NULL);
	ensure_required_parameters(s, -4, host, size);
	
	memset(&addr, 0, sizeof(addr));
	addr.sin_addr.s_addr = inet_addr(host);
	if (addr.sin_addr.s_addr == -1) {
		record = gethostbyname(host);
		if (!record) {
			close(sock);
			return 0;
		}
		
		addr_list = (struct in_addr**)record->h_addr_list;
		for(i = 0; addr_list[i]; i++) {
			addr.sin_addr = *addr_list[i];
			break;
		}
		
	}
	
	addr.sin_family = AF_INET;
	addr.sin_port = htons(80);
	
	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		close(sock);
		return 0;
	}
	
	su_string_begin(s, method);
	su_string_catf(s, " %s http/1.1\r\n", path);
	
	su_seq(s, -4, 0);
	while (su_type(s, -1) == SU_SEQ) {
		su_first(s, -1);
		su_first(s, -1);
		su_rest(s, -2);
		su_string_catf(s, "%s: %s\r\n", su_tostring(s, -2, NULL), su_stringify(s, -1));
		su_pop(s, 3);
		su_rest(s, -1);
		su_swap(s, -2, -1);
		su_pop(s, 1);
	}
	su_pop(s, 1);
	
	su_string_cat(s, "\r\n");
	if (size) {
		tmp = su_string_mem(s, size);
		memcpy(tmp, msg, size);
	}
	su_string_push(s);
	msg = su_tostring(s, -1, &size);
	
	su_thread_indisposable(s);
	do {
		i = send(sock, msg, size, 0);
		if (i < 0) {
			close(sock);
			return 0;
		}
		msg += i;
		size -= i;
	} while (size);
	su_thread_disposable(s);
	
	su_string_begin(s, NULL);
	
	su_thread_indisposable(s);
	do {
		i = recv(sock, su_scratchpad(s), SU_SCRATCHPAD_SIZE, 0);
		if (i < 0) {
			su_thread_disposable(s);
			su_string_push(s);
			close(sock);
			return 0;
		} else if (i > 0) {
			memcpy(su_string_mem(s, i), su_scratchpad(s), i);
		}
	} while (i > 0);
	su_thread_disposable(s);
	
	close(sock);
	su_string_push(s);
	parse_response(s);
	return 1;
	
}

#else

static int request(su_state *s, int narg) {
	return 0;
}

#endif

void libhttp(su_state *s) {
	int top;
	#if !defined(SU_OPT_NO_SOCKET) && defined(_WIN32)
		WSADATA wsa;
		if (!wsa_initialized) {
			wsa_initialized = 1;
			WSAStartup(MAKEWORD(2, 2), &wsa);
			atexit(&winsock_shutdown);
		}
	#endif
	top = su_top(s);
	
	su_pushstring(s, "request");
	su_pushfunction(s, &request);
	
	su_map(s, (su_top(s) - top) / 2);
	su_setglobal(s, "http");
}
