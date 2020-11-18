/*
 * Copyright (c) 2015-2020 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Red Hat, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>

#include "qnet-config.h"

#include "dynar.h"
#include "dynar-str.h"
#include "dynar-getopt-lex.h"
#include "log.h"
#include "nss-sock.h"
#include "pr-poll-array.h"
#include "qnetd-advanced-settings.h"
#include "qnetd-algorithm.h"
#include "qnetd-instance.h"
#include "qnetd-ipc.h"
#include "qnetd-client-net.h"
#include "qnetd-client-msg-received.h"
#include "utils.h"
#include "msg.h"

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

/*
 * This is global variable used for comunication with main loop and signal (calls close)
 */
struct qnetd_instance *global_instance;

enum tlv_decision_algorithm_type
    qnetd_static_supported_decision_algorithms[QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE] = {
	TLV_DECISION_ALGORITHM_TYPE_TEST,
	TLV_DECISION_ALGORITHM_TYPE_FFSPLIT,
	TLV_DECISION_ALGORITHM_TYPE_2NODELMS,
	TLV_DECISION_ALGORITHM_TYPE_LMS,
};

static void
qnetd_err_nss(void)
{

	log_nss(LOG_CRIT, "NSS error");

	exit(EXIT_FAILURE);
}

static void
qnetd_warn_nss(void)
{

	log_nss(LOG_WARNING, "NSS warning");
}

static int
server_socket_poll_loop_read_cb(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{
	struct qnetd_instance *instance = (struct qnetd_instance *)user_data1;

	qnetd_client_net_accept(instance);

	return (0);
}

static int
server_socket_poll_loop_err_cb(PRFileDesc *prfd, short revents, const PRPollDesc *pd,
    void *user_data1, void *user_data2)
{

	if (revents != POLLNVAL) {
		/*
		 * Poll ERR on listening socket is fatal error.
		 * POLL_NVAL is used as a signal to quit poll loop.
		 */
		log(LOG_CRIT, "POLL_ERR (%u) on listening socket", revents);
	} else {
		log(LOG_DEBUG, "Listening socket is closed");
	}

	return (-1);
}

static void
signal_int_handler(int sig)
{

	log(LOG_DEBUG, "SIGINT received - closing server IPC socket");

	qnetd_ipc_close(global_instance);
}

static void
signal_term_handler(int sig)
{

	log(LOG_DEBUG, "SIGTERM received - closing server IPC socket");

	qnetd_ipc_close(global_instance);
}

static void
signal_handlers_register(void)
{
	struct sigaction act;

	act.sa_handler = signal_int_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGINT, &act, NULL);

	act.sa_handler = signal_term_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGTERM, &act, NULL);
}

static int
qnetd_run_main_loop(struct qnetd_instance *instance)
{
	int poll_res;

	while ((poll_res = pr_poll_loop_exec(&instance->main_poll_loop)) == 0) {
	}

	if (poll_res == -2) {
		log(LOG_CRIT, "pr_poll_loop_exec returned -2 - internal error");
		return (-1);
	} else if (poll_res == -3) {
		log_nss(LOG_CRIT, "pr_poll_loop_exec returned -3 - PR_Poll error");
		return (-1);
	}

	return (qnetd_ipc_is_closed(instance) ? 0 : -1);
}

static void
usage(void)
{

	printf("usage: %s [-46dfhv] [-l listen_addr] [-p listen_port] [-s tls]\n", QNETD_PROGRAM_NAME);
	printf("%14s[-c client_cert_required] [-m max_clients] [-S option=value[,option2=value2,...]]\n", "");
}

static void
display_version(void)
{
	enum msg_type *supported_messages;
	size_t no_supported_messages;
	size_t zi;

	msg_get_supported_messages(&supported_messages, &no_supported_messages);
	printf("Corosync Qdevice Network Daemon, version '%s'\n\n", VERSION);
	printf("Supported algorithms: ");
	for (zi = 0; zi < QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE; zi++) {
		if (zi != 0) {
			printf(", ");
		}
		printf("%s (%u)",
		    tlv_decision_algorithm_type_to_str(qnetd_static_supported_decision_algorithms[zi]),
		    qnetd_static_supported_decision_algorithms[zi]);
	}
	printf("\n");
	printf("Supported message types: ");
	for (zi = 0; zi < no_supported_messages; zi++) {
		if (zi != 0) {
			printf(", ");
		}
		printf("%s (%u)", msg_type_to_str(supported_messages[zi]), supported_messages[zi]);
	}
	printf("\n");
}

static void
cli_parse_long_opt(struct qnetd_advanced_settings *advanced_settings, const char *long_opt)
{
	struct dynar_getopt_lex lex;
	struct dynar dynar_long_opt;
	const char *opt;
	const char *val;
	int res;

	dynar_init(&dynar_long_opt, strlen(long_opt) + 1);
	if (dynar_str_cpy(&dynar_long_opt, long_opt) != 0) {
		errx(EXIT_FAILURE, "Can't alloc memory for long option");
	}

	dynar_getopt_lex_init(&lex, &dynar_long_opt);

	while (dynar_getopt_lex_token_next(&lex) == 0 && strcmp(dynar_data(&lex.option), "") != 0) {
		opt = dynar_data(&lex.option);
		val = dynar_data(&lex.value);

		res = qnetd_advanced_settings_set(advanced_settings, opt, val);
		switch (res) {
		case -1:
			errx(EXIT_FAILURE, "Unknown option '%s'", opt);
			break;
		case -2:
			errx(EXIT_FAILURE, "Invalid value '%s' for option '%s'", val, opt);
			break;
		case -3:
			warnx("Option '%s' is deprecated and has no effect anymore", opt);
			break;
		}
	}

	dynar_getopt_lex_destroy(&lex);
	dynar_destroy(&dynar_long_opt);
}

static void
cli_parse(int argc, char * const argv[], char **host_addr, uint16_t *host_port, int *foreground,
    int *debug_log, int *bump_log_priority, enum tlv_tls_supported *tls_supported,
    int *client_cert_required, size_t *max_clients, PRIntn *address_family,
    struct qnetd_advanced_settings *advanced_settings)
{
	int ch;
	long long int tmpll;

	*host_addr = NULL;
	*host_port = QNETD_DEFAULT_HOST_PORT;
	*foreground = 0;
	*debug_log = 0;
	*bump_log_priority = 0;
	*tls_supported = QNETD_DEFAULT_TLS_SUPPORTED;
	*client_cert_required = QNETD_DEFAULT_TLS_CLIENT_CERT_REQUIRED;
	*max_clients = QNETD_DEFAULT_MAX_CLIENTS;
	*address_family = PR_AF_UNSPEC;

	while ((ch = getopt(argc, argv, "46dfhvc:l:m:p:S:s:")) != -1) {
		switch (ch) {
		case '4':
			*address_family = PR_AF_INET;
			break;
		case '6':
			*address_family = PR_AF_INET6;
			break;
		case 'f':
			*foreground = 1;
			break;
		case 'd':
			if (*debug_log) {
				*bump_log_priority = 1;
			}
			*debug_log = 1;
			break;
		case 'c':
			if ((*client_cert_required = utils_parse_bool_str(optarg)) == -1) {
				errx(EXIT_FAILURE, "client_cert_required should be on/yes/1, off/no/0");
			}
			break;
		case 'l':
			free(*host_addr);
			*host_addr = strdup(optarg);
			if (*host_addr == NULL) {
				errx(EXIT_FAILURE, "Can't alloc memory for host addr string");
			}
			break;
		case 'm':
			if (utils_strtonum(optarg, 0, LLONG_MAX, &tmpll) == -1) {
				errx(EXIT_FAILURE, "max clients value %s is invalid", optarg);
			}

			*max_clients = (size_t)tmpll;
			break;
		case 'p':
			if (utils_strtonum(optarg, 1, UINT16_MAX, &tmpll) == -1) {
				errx(EXIT_FAILURE, "host port must be in range 1-%u", UINT16_MAX);
			}

			*host_port = tmpll;
			break;
		case 'S':
			cli_parse_long_opt(advanced_settings, optarg);
			break;
		case 's':
			if (strcasecmp(optarg, "on") == 0) {
				*tls_supported = QNETD_DEFAULT_TLS_SUPPORTED;
			} else if (strcasecmp(optarg, "off") == 0) {
				*tls_supported = TLV_TLS_UNSUPPORTED;
			} else if (strcasecmp(optarg, "req") == 0) {
				*tls_supported = TLV_TLS_REQUIRED;
			} else {
				errx(EXIT_FAILURE, "tls must be one of on, off, req");
			}
			break;
		case 'v':
			display_version();
			exit(EXIT_FAILURE);
			break;
		case 'h':
		case '?':
			usage();
			exit(EXIT_FAILURE);
			break;
		}
	}
}

int
main(int argc, char * const argv[])
{
	struct qnetd_instance instance;
	struct qnetd_advanced_settings advanced_settings;
	char *host_addr;
	uint16_t host_port;
	int foreground;
	int debug_log;
	int bump_log_priority;
	enum tlv_tls_supported tls_supported;
	int client_cert_required;
	size_t max_clients;
	PRIntn address_family;
	int lock_file;
	int another_instance_running;
	int log_target;
	int main_loop_res;

	if (qnetd_advanced_settings_init(&advanced_settings) != 0) {
		errx(EXIT_FAILURE, "Can't alloc memory for advanced settings");
	}

	cli_parse(argc, argv, &host_addr, &host_port, &foreground, &debug_log, &bump_log_priority,
	    &tls_supported, &client_cert_required, &max_clients, &address_family, &advanced_settings);

	log_target = LOG_TARGET_SYSLOG;
	if (foreground) {
		log_target |= LOG_TARGET_STDERR;
	}

	if (log_init(QNETD_PROGRAM_NAME, log_target, LOG_DAEMON) == -1) {
		errx(EXIT_FAILURE, "Can't initialize logging");
	}

	log_set_debug(debug_log);
	log_set_priority_bump(bump_log_priority);

	/*
	 * Check that it's possible to open NSS dir if needed
	 */
	if (nss_sock_check_db_dir((tls_supported != TLV_TLS_UNSUPPORTED ?
	    advanced_settings.nss_db_dir : NULL)) != 0) {
		log_err(LOG_ERR, "Can't open NSS DB directory");

		return (EXIT_FAILURE);
	}

	/*
	 * Daemonize
	 */
	if (!foreground) {
		utils_tty_detach();
	}

	if ((lock_file = utils_flock(advanced_settings.lock_file, getpid(),
	    &another_instance_running)) == -1) {
		if (another_instance_running) {
			log(LOG_ERR, "Another instance is running");
		} else {
			log_err(LOG_ERR, "Can't acquire lock");
		}

		return (EXIT_FAILURE);
	}

	log(LOG_DEBUG, "Initializing nss");
	if (nss_sock_init_nss((tls_supported != TLV_TLS_UNSUPPORTED ?
	    advanced_settings.nss_db_dir : NULL)) != 0) {
		qnetd_err_nss();
	}

	if (SSL_ConfigServerSessionIDCache(0, 0, 0, NULL) != SECSuccess) {
		qnetd_err_nss();
	}

	if (qnetd_instance_init(&instance, tls_supported, client_cert_required,
	    max_clients, &advanced_settings) == -1) {
		log(LOG_ERR, "Can't initialize qnetd");
		return (EXIT_FAILURE);
	}
	instance.host_addr = host_addr;
	instance.host_port = host_port;

	if (tls_supported != TLV_TLS_UNSUPPORTED && qnetd_instance_init_certs(&instance) == -1) {
		qnetd_err_nss();
	}

	log(LOG_DEBUG, "Initializing local socket");
	if (qnetd_ipc_init(&instance) != 0) {
		return (EXIT_FAILURE);
	}

	log(LOG_DEBUG, "Creating listening socket");
	instance.server.socket = nss_sock_create_listen_socket(instance.host_addr,
	    instance.host_port, address_family);
	if (instance.server.socket == NULL) {
		qnetd_err_nss();
	}

	if (nss_sock_set_non_blocking(instance.server.socket) != 0) {
		qnetd_err_nss();
	}

	if (PR_Listen(instance.server.socket, instance.advanced_settings->listen_backlog) !=
	    PR_SUCCESS) {
		qnetd_err_nss();
	}

	if (pr_poll_loop_add_prfd(&instance.main_poll_loop, instance.server.socket, POLLIN,
	    NULL,
	    server_socket_poll_loop_read_cb,
	    NULL,
	    server_socket_poll_loop_err_cb,
	    &instance, NULL) != 0) {
		log(LOG_ERR, "Can't add server socket to main poll loop");

		return (EXIT_FAILURE);
	}

	global_instance = &instance;
	signal_handlers_register();

	log(LOG_DEBUG, "Registering algorithms");
	if (qnetd_algorithm_register_all() != 0) {
		return (EXIT_FAILURE);
	}

	log(LOG_DEBUG, "QNetd ready to provide service");

#ifdef HAVE_LIBSYSTEMD
	sd_notify(0, "READY=1");
#endif

	log(LOG_DEBUG, "Running QNetd main loop");
	main_loop_res = qnetd_run_main_loop(&instance);

	/*
	 * Cleanup
	 */
	log(LOG_DEBUG, "Destroying qnetd ipc");
	qnetd_ipc_destroy(&instance);

	log(LOG_DEBUG, "Closing server socket");
	if (PR_Close(instance.server.socket) != PR_SUCCESS) {
		qnetd_warn_nss();
	}

	CERT_DestroyCertificate(instance.server.cert);
	SECKEY_DestroyPrivateKey(instance.server.private_key);

	SSL_ClearSessionCache();

	SSL_ShutdownServerSessionIDCache();

	qnetd_instance_destroy(&instance);

	qnetd_advanced_settings_destroy(&advanced_settings);

	if (NSS_Shutdown() != SECSuccess) {
		qnetd_warn_nss();
	}

	if (PR_Cleanup() != PR_SUCCESS) {
		qnetd_warn_nss();
	}

	log(LOG_DEBUG, "Closing log");
	log_close();

	return (main_loop_res == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
