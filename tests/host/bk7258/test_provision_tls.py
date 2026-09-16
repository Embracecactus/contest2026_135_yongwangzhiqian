#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual product TLS over fragmented fake GATT, using workspace mbedTLS.

Named host gate: python3 tests/host/bk7258/test_provision_tls.py.
Builds crypto out of tree; ephemeral test-only identity is removed afterward.
MBEDTLS_SOURCE may select another checkout for upstream compatibility testing.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class ProvisionTlsTest(unittest.TestCase):
    def test_real_tls_fragmentation_and_teardown(self):
        source = Path(os.environ.get('MBEDTLS_SOURCE',
                      ROOT.parent / 'apps/crypto/mbedtls/mbedtls'))
        self.assertTrue((source / 'CMakeLists.txt').is_file())
        with tempfile.TemporaryDirectory(prefix='bkprov-tls-test-') as directory:
            temp = Path(directory)
            (temp / 'include/nuttx').mkdir(parents=True)
            (temp / 'include/nuttx/config.h').write_text('')
            with (temp / 'build.log').open('w+') as log:
                def run(args, env=None):
                    result = subprocess.run([str(a) for a in args],
                                            stdout=log, stderr=log, env=env)
                    if result.returncode:
                        log.seek(0)
                        self.fail(log.read()[-6000:])
                run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                     '-I', temp / 'include', '-I', source / 'include', '-I', ROOT / 'app/bk7258',
                     ROOT / 'tests/host/bk7258/test_provision_owner.c',
                     ROOT / 'app/bk7258/bk7258_provision_owner.c',
                     ROOT / 'app/bk7258/bk7258_provision_scan.c',
                     '-o', temp / 'owner'])
                run([temp / 'owner'])
                chip_include = temp / 'include/arch/chip'
                chip_include.mkdir(parents=True)
                (chip_include / 'bk7258_wifi.h').symlink_to(ROOT / 'chips/bk7258/include/bk7258_wifi.h')
                run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                     '-DCONFIG_BK7258_WIFI_VNET', '-DCONFIG_BK7258_AP_CORE',
                     '-I', temp / 'include', '-I', source / 'include', '-I', ROOT / 'app/bk7258',
                     ROOT / 'tests/host/bk7258/test_provision_network.c',
                     ROOT / 'app/bk7258/bk7258_provision_network.c',
                     '-Wl,--wrap=clock_settime', '-o', temp / 'network'])
                run([temp / 'network'])
                run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                     '-DFAR=', '-DCONFIG_NETUTILS_NTPCLIENT_NUM_SAMPLES=3',
                     '-I', ROOT / 'tests/host/bk7258/mocks',
                     '-I', ROOT.parent / 'apps/include', '-I', ROOT / 'app/bk7258',
                     ROOT / 'tests/host/bk7258/test_provision_time.c',
                     ROOT / 'app/bk7258/bk7258_provision_time.c',
                     '-Wl,--wrap=clock_gettime,--wrap=waitpid', '-o', temp / 'time'])
                run([temp / 'time'])
                build = temp / 'build'
                run(['cmake', '-S', source, '-B', build,
                     '-DENABLE_PROGRAMS=OFF', '-DENABLE_TESTING=OFF',
                     # Workspace mbedTLS has one PSA helper without a prior
                     # prototype. Keep its warning visible; product C below
                     # is still compiled with full -Werror.
                     '-DCMAKE_C_FLAGS=-Wno-error=missing-prototypes',
                     '-DCMAKE_BUILD_TYPE=Release'])
                run(['cmake', '--build', build, '-j8'])
                run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                     '-I', source / 'include', '-I', ROOT / 'app/bk7258',
                     ROOT / 'tests/host/bk7258/test_provision_settings.c',
                     ROOT / 'app/bk7258/bk7258_provision_settings.c',
                     ROOT / 'app/bk7258/bk7258_cloud_config.c',
                     ROOT / 'app/bk7258/bk7258_voice_config.c',
                     '-Wl,--wrap=clock_settime',
                     build / 'library/libmbedx509.a',
                     build / 'library/libmbedcrypto.a', '-o', temp / 'settings'])
                run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                     '-DCONFIG_BK7258_WIFI_VNET', '-DCONFIG_BK7258_AP_CORE',
                     '-I', temp / 'include', '-I', source / 'include', '-I', ROOT / 'app/bk7258',
                     ROOT / 'tests/host/bk7258/test_provision_tls.c',
                     ROOT / 'app/bk7258/bk7258_provision_tls.c',
                     ROOT / 'app/bk7258/bk7258_provision_claim.c',
                     ROOT / 'app/bk7258/bk7258_provision_store.c',
                     ROOT / 'app/bk7258/bk7258_provision_pair.c',
                     ROOT / 'app/bk7258/bk7258_provision_scan.c',
                     ROOT / 'app/bk7258/bk7258_control_pair.c',
                     ROOT / 'app/bk7258/bk7258_control_session.c',
                     build / 'library/libmbedtls.a',
                     build / 'library/libmbedx509.a',
                     build / 'library/libmbedcrypto.a', '-o', temp / 'test'])
                if os.environ.get('SHANIU_ANDROID_INTEROP') == '1':
                    run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                         '-I', ROOT / 'app/bk7258',
                         ROOT / 'tests/host/bk7258/test_control_session.c',
                         ROOT / 'app/bk7258/bk7258_control_session.c',
                         '-o', temp / 'control-peer'])
                    run([ROOT / 'android/shaniu-companion/gradlew',
                         '-p', ROOT / 'android/shaniu-companion',
                         ':app:testDebugUnitTest',
                         '--offline'], env={**os.environ,
                                           'SHANIU_CONTROL_TLS_PEER': str(temp / 'test'),
                                           'SHANIU_CONTROL_PEER': str(temp / 'control-peer')})
                run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                     '-I', source / 'include', '-I', ROOT / 'app/bk7258',
                     ROOT / 'tests/host/bk7258/test_provision_claim.c',
                     ROOT / 'app/bk7258/bk7258_provision_claim.c',
                     ROOT / 'app/bk7258/bk7258_provision_store.c',
                     '-Wl,--wrap=write,--wrap=fsync,--wrap=rename',
                     build / 'library/libmbedcrypto.a', '-o', temp / 'claim'])
                run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                     '-I', source / 'include', '-I', ROOT / 'app/bk7258',
                     ROOT / 'tests/host/bk7258/test_provision_identity.c',
                     ROOT / 'app/bk7258/bk7258_provision_identity.c',
                     build / 'library/libmbedx509.a', build / 'library/libmbedcrypto.a',
                     '-o', temp / 'identity'])
                private = temp / 'private'
                run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                     '-I', source / 'include', '-I', ROOT / 'app/bk7258',
                     ROOT / 'tests/host/bk7258/test_provision_storage.c',
                     ROOT / 'app/bk7258/bk7258_provision_store.c',
                     ROOT / 'app/bk7258/bk7258_provision_storage.c',
                     '-Wl,--wrap=fsync,--wrap=rename', build / 'library/libmbedcrypto.a',
                     '-o', temp / 'storage'])
                storage_root = temp / 'storage-private'
                run([temp / 'storage', storage_root])
                run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                     '-I', ROOT / 'tests/host/bk7258/mocks',
                     '-I', source / 'include', '-I', ROOT / 'app/bk7258',
                     ROOT / 'tests/host/bk7258/test_bk7258_voice_volume_store.c',
                     ROOT / 'app/bk7258/bk7258_voice_volume_store.c',
                     ROOT / 'app/bk7258/bk7258_provision_store.c',
                     '-Wl,--wrap=fsync,--wrap=rename',
                     build / 'library/libmbedcrypto.a', '-o', temp / 'volume-store'])
                run([temp / 'volume-store', temp / 'voice-volume'])
                private.mkdir(mode=0o700)
                run([temp / 'claim', private])
                # Vary both certificate and ephemeral handshake lengths.
                for index in range(20):
                    run(['openssl', 'req', '-x509', '-newkey', 'ec',
                         '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes',
                         '-keyout', temp / 'key.pem', '-out', temp / 'cert.pem',
                         '-subj', '/CN=localhost', '-days', '1',
                         '-addext', 'subjectAltName=DNS:localhost'])
                    pair_store = temp / f'pair-{index}'
                    pair_store.mkdir(mode=0o700)
                    run([temp / 'test', temp / 'cert.pem', temp / 'key.pem', pair_store])
                    if index == 0:
                        run(['openssl', 'x509', '-in', temp / 'cert.pem',
                             '-outform', 'DER', '-out', temp / 'cert.der'])
                        run(['openssl', 'pkcs8', '-topk8', '-nocrypt',
                             '-in', temp / 'key.pem', '-outform', 'DER',
                             '-out', temp / 'key.der'])
                        run([temp / 'settings', temp / 'cert.der', temp / 'key.der'])
                        run(['openssl', 'req', '-x509', '-key', temp / 'key.pem',
                             '-out', temp / 'identity.der', '-outform', 'DER', '-days', '1',
                             '-subj', '/CN=test-device', '-addext', 'basicConstraints=critical,CA:FALSE',
                             '-addext', 'keyUsage=critical,digitalSignature',
                             '-addext', 'extendedKeyUsage=serverAuth,clientAuth'])
                        run([temp / 'identity', temp / 'identity.der', temp / 'key.der', temp / 'cert.der'])


if __name__ == '__main__':
    unittest.main()
