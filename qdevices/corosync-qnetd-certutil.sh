#!@BASHPATH@

#
# Copyright (c) 2015-2026 Red Hat, Inc.
#
# All rights reserved.
#
# Author: Jan Friesse (jfriesse@redhat.com)
#
# This software licensed under BSD license, the text of which follows:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the Red Hat, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.
#

CONFIG_DIR="@COROSYSCONFDIR@/qnetd"
DB_DIR="$CONFIG_DIR/nssdb"
# Validity of certificate (months)
CRT_VALIDITY=1200
CA_NICKNAME="QNet CA"
SERVER_NICKNAME="QNetd Cert"
SERVER_SUBJECT="CN=Qnetd Server"
PWD_FILE="$DB_DIR/pwdfile.txt"
NOISE_FILE="$DB_DIR/noise.txt"
SERIAL_NO_FILE="$DB_DIR/serial.txt"
CA_EXPORT_FILE="$DB_DIR/qnetd-cacert.crt"
CERTDB_FILES=("cert9.db key4.db pkcs11.txt"
              "cert8.db key3.db secmod.db")

# errx exit_code message
errx() {
    echo "$2" >&2

    exit "$1"
}

usage() {
    echo "$0: [-I|-i|-s] [-C ca_nickname] [-c certificate] [-G] [-g keysize] [-k keytype] [-n cluster_name] [-q paramset] [-S serv_nickname]"
    echo
    echo " -I                  Generate and add another QNetd CA and new server certificate"
    echo " -i                  Initialize QNetd CA and generate server certificate"
    echo " -s                  Sign cluster certificate (needs cluster certificate)"
    echo " -C ca_nickname      NSS database QNetd CA certificate nickname"
    echo " -c certificate      CRQ certificate file name"
    echo " -G                  Do not set group write bit for new files"
    echo " -g keysize          Key size in bits - passed directly to certutil as -g parameter"
    echo " -k keytype          Type of key - passed directly to certutil as -k parameter"
    echo " -n cluster_name     Name of cluster (for -s operation)"
    echo " -q paramset         Parameter set (curve name, ml-dsa set) - passed directly to certutil as -q parameter"
    echo " -S serv_nickname    NSS database QNetd server certificate nickname"

    exit 0
}

chown_ref_cfgdir() {
    if [ "$UID" == "0" ];then
        chown --reference="$CONFIG_DIR" "$@" 2>/dev/null && return 0
        if command -v stat >/dev/null;then
            chown "$(stat -c "%u:%g" "$CONFIG_DIR")" "$@" 2>/dev/null && return 0
            chown "$(stat -f "%u:%g" "$CONFIG_DIR")" "$@" 2>/dev/null && return 0
        fi
        chown "$(ls -nd "$CONFIG_DIR" | awk '{ print $3 ":" $4 }')" "$@" 2>/dev/null || return $?
    fi
}

# get_perm [directory]
# Return permission based on -G and directory flag
get_perm() {
    directory=${1:-}

    if [ "$directory" == true ];then
        [ "$SET_GROUP_WRITE_BIT" == true ] && echo "0770" || echo "0750"
    else
        [ "$SET_GROUP_WRITE_BIT" == true ] && echo "0660" || echo "0640"
    fi
}

create_new_noise_file() {
    local noise_file="$1"

    if [ ! -e "$noise_file" ];then
        echo "Creating new noise file $noise_file"

        if command -v sha1sum >/dev/null; then
            (ps -elf; date; w) | sha1sum | (read sha_sum rest; echo $sha_sum) > "$noise_file"
        else
            dd if=/dev/urandom of="$noise_file" bs=64 count=1 2>/dev/null
        fi

        chown_ref_cfgdir "$noise_file"
        chmod "$(get_perm)" "$noise_file"
    else
        echo "Using existing noise file $noise_file"
    fi
}

get_serial_no() {
    local serial_no

    if ! [ -f "$SERIAL_NO_FILE" ];then
        echo "100" > $SERIAL_NO_FILE
        chown_ref_cfgdir "$SERIAL_NO_FILE"
        chmod "$(get_perm)" "$SERIAL_NO_FILE"
    fi
    serial_no=`cat $SERIAL_NO_FILE`
    serial_no=$((serial_no+1))
    echo "$serial_no" > $SERIAL_NO_FILE
    echo "$serial_no"
}

find_certdb_files() {
    for cert_files_index in "${!CERTDB_FILES[@]}";do
        cert_files=${CERTDB_FILES[$cert_files_index]}
        test_file=${cert_files%% *}
        if [ -f "$DB_DIR/$test_file" ];then
            echo "$cert_files"

            return 0
        fi
    done
}

get_certutil_key_params() {
    CERTUTIL_PARAMS=""

    if [ ! -z "$COROSYNC_QNETD_CERTUTIL_KEY_SIZE" ];then
        CERTUTIL_PARAMS="$CERTUTIL_PARAMS -g $COROSYNC_QNETD_CERTUTIL_KEY_SIZE"
    fi

    if [ ! -z "$COROSYNC_QNETD_CERTUTIL_KEY_TYPE" ];then
        CERTUTIL_PARAMS="$CERTUTIL_PARAMS -k $COROSYNC_QNETD_CERTUTIL_KEY_TYPE"
    fi

    if [ ! -z "$COROSYNC_QNETD_CERTUTIL_PARAMSET" ];then
        CERTUTIL_PARAMS="$CERTUTIL_PARAMS -q $COROSYNC_QNETD_CERTUTIL_PARAMSET"
    fi

    echo "$CERTUTIL_PARAMS"
}

nss_nickname_exists() {
    certutil -L -d "$DB_DIR" -n "$1" >/dev/null 2>&1
}

add_qnetd_ca() {
    cert_files=`find_certdb_files`
    if [ "$cert_files" == "" ];then
        errx 1 "Certificate database doesn't exist. Use $0 -i to create it"
    fi

    if nss_nickname_exists "$CA_NICKNAME";then
        errx 1 "Certificate nickname \"$CA_NICKNAME\" already exists in the database."
    fi

    if nss_nickname_exists "$SERVER_NICKNAME";then
        errx 1 "Certificate nickname \"$SERVER_NICKNAME\" already exists in the database."
    fi

    echo "Creating new CA"
    # Create self-signed certificate (CA). Asks 3 questions (is this CA, lifetime and critical extension
    echo -e "y\n0\ny\n" | certutil -S -n "$CA_NICKNAME" -s "$CA_SUBJECT" -x \
        -t "CT,," -m "$(get_serial_no)" -v $CRT_VALIDITY -d "$DB_DIR" \
        -z "$NOISE_FILE" -f "$PWD_FILE" -2 $(get_certutil_key_params)

    # Export CA certificate in ascii
    certutil -L -d "$DB_DIR" -n "$CA_NICKNAME" > "$CA_EXPORT_FILE"
    certutil -L -d "$DB_DIR" -n "$CA_NICKNAME" -a >> "$CA_EXPORT_FILE"
    chown_ref_cfgdir "$CA_EXPORT_FILE"

    certutil -S -n "$SERVER_NICKNAME" -s "$SERVER_SUBJECT" -c "$CA_NICKNAME" -t "u,u,u" -m "$(get_serial_no)" \
        -v $CRT_VALIDITY -d "$DB_DIR" -z "$NOISE_FILE" -f "$PWD_FILE" $(get_certutil_key_params)

    echo "QNetd CA certificate is exported as $CA_EXPORT_FILE"
}

init_qnetd_ca() {
    cert_files=`find_certdb_files`
    if [ "$cert_files" != "" ];then
        errx 1 "Certificate database ($DB_DIR) already exists. Delete it to initialize new db"
    fi

    if ! [ -d "$DB_DIR" ];then
        echo "Creating $DB_DIR"
        mkdir -p "$DB_DIR"
        chown_ref_cfgdir "$DB_DIR"
        chmod "$(get_perm true)" "$DB_DIR"
    fi

    echo "Creating new key and cert db"
    echo -n "" > "$PWD_FILE"
    chown_ref_cfgdir "$PWD_FILE"
    chmod "$(get_perm)" "$PWD_FILE"

    certutil -N -d "$DB_DIR" -f "$PWD_FILE"
    cert_files=`find_certdb_files`
    if [ "$cert_files" == "" ];then
        errx 1 "Can't find certificate database files. Certificate database ($DB_DIR) cannot be created"
    fi

    for fname in $cert_files;do
        chown_ref_cfgdir "$DB_DIR/$fname"
        chmod "$(get_perm)" "$DB_DIR/$fname"
    done

    create_new_noise_file "$NOISE_FILE"

    add_qnetd_ca
}


sign_cluster_cert() {
    cert_files=`find_certdb_files`
    if [ "$cert_files" == "" ];then
        errx 1 "Certificate database doesn't exists. Use $0 -i to create it"
    fi

    echo "Signing cluster certificate"
    certutil -C -v "$CRT_VALIDITY" -m "$(get_serial_no)" -i "$CERTIFICATE_FILE" -o "$CRT_FILE" -c "$CA_NICKNAME" -d "$DB_DIR"
    chown_ref_cfgdir "$CRT_FILE"

    echo "Certificate stored in $CRT_FILE"
}

# Initialize options that may be overwritten by the configuration file
COROSYNC_QNETD_CERTUTIL_KEY_SIZE=""
COROSYNC_QNETD_CERTUTIL_KEY_TYPE=""
COROSYNC_QNETD_CERTUTIL_PARAMSET=""

# Import configuration file if it exists
if [ -f "@INITCONFIGDIR@/corosync-qnetd" ];then
    . "@INITCONFIGDIR@/corosync-qnetd"
fi

# Strict mode
set -euo pipefail
trap 's=$?; echo >&2 "$0: Error on line "$LINENO": $BASH_COMMAND"; exit $s' ERR

OPERATION=""
CERTIFICATE_FILE=""
CLUSTER_NAME=""
SET_GROUP_WRITE_BIT=true

while getopts ":IGhisC:c:g:k:n:q:S:" opt; do
    case $opt in
        I)
            OPERATION=add_qnetd_ca
            ;;
        i)
            OPERATION=init_qnetd_ca
            ;;
        s)
            OPERATION=sign_cluster_cert
            ;;
        h)
            usage
            ;;
        C)
            CA_NICKNAME="$OPTARG"
            CA_EXPORT_FILE="$DB_DIR/qnetd-cacert-$CA_NICKNAME.crt"
            ;;
        c)
            CERTIFICATE_FILE="$OPTARG"
            ;;
        G)
            SET_GROUP_WRITE_BIT=false
            ;;
        g)
            COROSYNC_QNETD_CERTUTIL_KEY_SIZE="$OPTARG"
            ;;
        k)
            COROSYNC_QNETD_CERTUTIL_KEY_TYPE="$OPTARG"
            ;;
        n)
            CLUSTER_NAME="$OPTARG"
            ;;
        q)
            COROSYNC_QNETD_CERTUTIL_PARAMSET="$OPTARG"
            ;;
        S)
            SERVER_NICKNAME="$OPTARG"
            ;;
        \?)
            errx 1 "Invalid option: -$OPTARG"
            ;;
        :)
            errx 1 "Option -$OPTARG requires an argument."
            ;;
   esac
done

[ "$OPERATION" == "" ] && usage

CRT_FILE="$DB_DIR/cluster-$CLUSTER_NAME.crt"
CA_SUBJECT="CN=$CA_NICKNAME"

case "$OPERATION" in
    "add_qnetd_ca")
        add_qnetd_ca
    ;;
    "init_qnetd_ca")
        init_qnetd_ca
    ;;
    "sign_cluster_cert")
        if ! [ -e "$CERTIFICATE_FILE" ];then
            errx 2 "Can't open certificate file $CERTIFICATE_FILE"
        fi

        if [ "$CLUSTER_NAME" == "" ];then
            errx 2 "You have to specify cluster name"
        fi

        sign_cluster_cert
    ;;
    *)
        usage
    ;;
esac
