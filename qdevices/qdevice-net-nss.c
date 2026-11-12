/*
 * Copyright (c) 2015-2026 Red Hat, Inc.
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

#include <sslerr.h>
#include <secerr.h>

#include "log.h"
#include "qdevice-net-nss.h"
#include "qdevice-net-instance.h"
#include "qnet-config.h"

SECStatus
qdevice_net_nss_bad_cert_hook(void *arg, PRFileDesc *fd) {
	if (PR_GetError() == SEC_ERROR_EXPIRED_CERTIFICATE ||
	    PR_GetError() == SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE ||
	    PR_GetError() == SEC_ERROR_CRL_EXPIRED ||
	    PR_GetError() == SEC_ERROR_KRL_EXPIRED ||
	    PR_GetError() == SSL_ERROR_EXPIRED_CERT_ALERT) {
		log(LOG_WARNING, "Server certificate is expired.");

		return (SECSuccess);
	}

	log_nss(LOG_ERR, "Server certificate verification failure.");

	return (SECFailure);
}

SECStatus
qdevice_net_nss_get_client_auth_data(void *arg, PRFileDesc *sock, struct CERTDistNamesStr *caNames,
    struct CERTCertificateStr **pRetCert, struct SECKEYPrivateKeyStr **pRetKey)
{
	struct qdevice_net_instance *instance;

	log(LOG_DEBUG, "Sending client auth data.");

	instance = (struct qdevice_net_instance *)arg;

	instance->tls_client_cert_sent = 1;

	return (NSS_GetClientAuthData((void *)instance->advanced_settings->net_nss_client_cert_nickname,
	    sock, caNames, pRetCert, pRetKey));
}

static void
qdevice_net_nss_dump_cert_info(const char *prefix, CERTCertificate *cert)
{
	char *nss_subject, *nss_issuer;
	const char *subject, *issuer;
	SECOidData *signature_oid_data, *public_key_oid_data;
	const char *signature_algorithm_desc, *public_key_algorithm_desc;

	nss_subject = CERT_NameToAscii(&cert->subject);
	subject = (nss_subject != NULL ? nss_subject : "Unknown");

	nss_issuer = CERT_NameToAscii(&cert->issuer);
	issuer = (nss_issuer != NULL ? nss_issuer : "Unknown");

	signature_oid_data = SECOID_FindOID(&cert->signature.algorithm);
	signature_algorithm_desc = ((signature_oid_data != NULL && signature_oid_data->desc != NULL) ?
	    signature_oid_data->desc : "Unknown");

	public_key_oid_data = SECOID_FindOID(&cert->subjectPublicKeyInfo.algorithm.algorithm);
	public_key_algorithm_desc = ((public_key_oid_data != NULL && public_key_oid_data->desc != NULL) ?
	    public_key_oid_data->desc : "Unknown");

	log(LOG_DEBUG, "  %s certificate Subject: %s, Issuer: %s, Signature Algorithm: %s, Public Key Algorithm: %s",
	    prefix, subject, issuer, signature_algorithm_desc, public_key_algorithm_desc);

	if (nss_subject != NULL) {
		PORT_Free(nss_subject);
	}

	if (nss_issuer != NULL) {
		PORT_Free(nss_issuer);
	}
}

void
qdevice_net_nss_handshake_callback(PRFileDesc *fd, void *client_data)
{
	SSLChannelInfo ci = { 0 };
	CERTCertificate *cert;

	if (SSL_GetChannelInfo(fd, &ci, sizeof(ci)) == SECSuccess) {
		/*
		 * No easy way to decode this numbers so they are written unencoded.
		 * Values are in the /usr/include/nss3/sslt.h
		 * - SSLKEAType - ssl_kea_ecdh = 4, ssl_kea_ecdh_hybrid = 8 (PQC)
		 * - SSLNamedGroup - ssl_grp_ec_secp256r1 = 23,ssl_grp_kem_mlkem768x25519 = 4588 (PQC)
		 */
		log(LOG_DEBUG, "Using TLS channel protocol version: %04x, keaType: %u, keaGroup: %u",
		    ci.protocolVersion, ci.keaType, ci.keaGroup);
	} else {
		log_nss(LOG_WARNING, "qdevice_net_nss_handshake_callback SSL_GetChannelInfo error");
	}

	cert = SSL_PeerCertificate(fd);
	if (cert != NULL) {
		qdevice_net_nss_dump_cert_info("Peer", cert);

		CERT_DestroyCertificate(cert);
	}

	cert = SSL_LocalCertificate(fd);
	if (cert != NULL) {
		qdevice_net_nss_dump_cert_info("Local", cert);

		CERT_DestroyCertificate(cert);
	}
}
