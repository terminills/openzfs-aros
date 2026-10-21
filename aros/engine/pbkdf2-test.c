/* SPDX-License-Identifier: CDDL-1.0 */
/* Public RFC 6070 known-answer vectors; no user key material. */
#include <openssl/evp.h>
#include <proto/exec.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "passphrase.h"
int main(void)
{
    static const struct {unsigned iterations;uint8_t expected[20];} tests[]={
        {1,{0x0c,0x60,0xc8,0x0f,0x96,0x1f,0x0e,0x71,0xf3,0xa9,0xb5,0x24,0xaf,0x60,0x12,0x06,0x2f,0xe0,0x37,0xa6}},
        {2,{0xea,0x6c,0x01,0x4d,0xc7,0x2d,0x6f,0x8c,0xcd,0x1e,0xd9,0x2a,0xce,0x1d,0x41,0xf0,0xd8,0xde,0x89,0x57}},
        {4096,{0x4b,0x00,0x79,0x01,0xb7,0x65,0x48,0x9a,0xbe,0xad,0x49,0xd9,0x26,0xf7,0x21,0xd0,0x65,0xa4,0x29,0xc1}}
    };
    uint8_t key[20];int ok=1;
    for(unsigned i=0;i<sizeof(tests)/sizeof(tests[0]);i++)
        if(PKCS5_PBKDF2_HMAC_SHA1("password",8,(const uint8_t *)"salt",4,
            tests[i].iterations,sizeof(key),key)!=1||memcmp(key,tests[i].expected,sizeof(key)))ok=0;
    /* Independently generated with host Python hashlib.pbkdf2_hmac SHA1:
     * OpenZFS's 32-byte output spans two SHA1 blocks; salt is little endian. */
    static const uint8_t expected32[]={0x0b,0x6f,0x44,0xb6,0x22,0x3f,0x32,0xb7,
        0x03,0x14,0xa7,0x53,0xa0,0x3e,0x1e,0x23,0x4d,0xe0,0x53,0x96,0xb4,0x0f,
        0x35,0x9e,0x85,0x49,0xda,0xbb,0xa2,0x41,0xa9,0x24};
    static const uint8_t salt[]={8,7,6,5,4,3,2,1};
    const char *password="AROS OpenZFS fixture passphrase";uint8_t key32[32];
    if(PKCS5_PBKDF2_HMAC_SHA1(password,strlen(password),salt,sizeof(salt),350000,
        sizeof(key32),key32)!=1||memcmp(key32,expected32,sizeof(key32)))ok=0;
    if(oz_passphrase_derive(password,strlen(password),UINT64_C(0x0102030405060708),
        350000,key32)||memcmp(key32,expected32,sizeof(key32)))ok=0;
    static const uint8_t zero[32]={0};
    static const uint64_t invalid[]={0,UINT64_C(0x100000000),UINT64_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        memset(key32,0xa5,sizeof(key32));
        if(oz_passphrase_derive(password,strlen(password),1,invalid[i],key32)!=EINVAL||
            memcmp(key32,zero,sizeof(key32)))ok=0;
    }
    memset(key32,0xa5,sizeof(key32));
    if(oz_passphrase_derive(NULL,0,1,1,key32)!=EINVAL||memcmp(key32,zero,sizeof(key32))||
        oz_passphrase_derive(password,strlen(password),1,1,NULL)!=EINVAL)ok=0;
    const char *text=ok?"RESULT native PBKDF2 known answers PASS\n":"RESULT native PBKDF2 known answers FAIL\n";
    for(const char *p=text;*p;p++)RawPutChar(*p);
    return ok?0:20;
}
