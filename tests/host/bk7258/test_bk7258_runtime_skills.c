/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_runtime_skills.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include <mbedtls/base64.h>
#include "bk7258_provision_store.h"
#include <unistd.h>
#include <sys/wait.h>

static int g_commit_failure;
int __real_bkprov_store_commit(struct bkprov_store_s *store, uint64_t expected,
                               const void *transaction, const void *bundle,
                               size_t size);
int __wrap_bkprov_store_commit(struct bkprov_store_s *store, uint64_t expected,
                               const void *transaction, const void *bundle,
                               size_t size)
{
 if (g_commit_failure) return g_commit_failure;
 return __real_bkprov_store_commit(store, expected, transaction, bundle, size);
}

int main(int argc, char **argv)
{
 static const unsigned char key[] = {0x30,0x59,0x30,0x13,0x06,0x07,0x2a,0x86,0x48,0xce,0x3d,0x02,0x01,0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0x03,0x42,0x00,0x04,0x77,0x72,0x65,0x6f,0x81,0x4b,0x39,0x92,0x79,0xd5,0xe1,0xf1,0x78,0x1f,0xac,0x6f,0x09,0x9a,0x3c,0x5c,0xa1,0xb0,0xe3,0x53,0x51,0x83,0x4b,0x08,0xb6,0x5e,0x0b,0x57,0x25,0x90,0xcd,0xaf,0x8f,0x76,0x93,0x61,0xbc,0xf3,0x4a,0xcf,0xc1,0x1e,0x5e,0x07,0x4e,0x84,0x26,0xbd,0xde,0x04,0xbe,0x6e,0x65,0x39,0x45,0x44,0x96,0x17,0xde,0x45};
 static const unsigned char signature[] = {0x30,0x45,0x02,0x20,0x60,0xc0,0xac,0xda,0x75,0x6d,0xfe,0x02,0x43,0x32,0x92,0x7c,0xc4,0x18,0xb7,0x64,0x36,0xf6,0xb7,0xff,0xd2,0x88,0x96,0xe6,0x22,0x00,0xb3,0xb3,0xbd,0x03,0x90,0x84,0x02,0x21,0x00,0xe7,0xca,0x83,0x7f,0xc3,0x7f,0x78,0x21,0xdf,0xd8,0x3d,0x41,0x74,0x7f,0xdc,0x98,0xec,0x3b,0xca,0x89,0xe5,0xa6,0xab,0xd4,0xa9,0x76,0x9b,0x4d,0x01,0x45,0x33,0xac};
 static const char manifest[] = "{\"id\":\"custom\",\"name\":\"Custom\",\"profile\":\"focus-coach\",\"triggers\":[\"定制助手\"],\"tools\":[\"focus_timer\"],\"permissions\":[\"focus_timer\"],\"body\":\"# custom\"}\n";
 char prompt[1536]; const char *doc;
 if (argc == 5 && !strcmp(argv[1], "--check-vector"))
   {
     unsigned char bytes[2049], sig[256];
     FILE *file = fopen(argv[2], "rb"); assert(file);
     size_t length = fread(bytes, 1, sizeof(bytes) - 1, file);
     assert(!ferror(file) && fclose(file) == 0); bytes[length] = 0;
     file = fopen(argv[3], "rb"); assert(file);
     size_t sig_size = fread(sig, 1, sizeof(sig), file);
     assert(!ferror(file) && fclose(file) == 0);
     assert(bk7258_runtime_skills_set_owner_key(key, sizeof(key)) == 0);
     assert(bk7258_runtime_skills_install(bytes, length, sig, sig_size) == atoi(argv[4]));
     if (atoi(argv[4]) == 0)
       {
         char read[4096], trigger[80];
         assert(bk7258_runtime_skills_app_read(read, sizeof(read)) == 0);
         cJSON *manifest_object = cJSON_Parse((const char *)bytes);
         cJSON *state = cJSON_Parse(read); assert(state && manifest_object);
         cJSON *custom = cJSON_GetArrayItem(cJSON_GetObjectItem(state,"items"),3);
         assert(!strcmp(cJSON_GetObjectItem(custom,"name")->valuestring,
                        cJSON_GetObjectItem(manifest_object,"name")->valuestring));
         assert(cJSON_GetArraySize(cJSON_GetObjectItem(custom,"permissions")) ==
                cJSON_GetArraySize(cJSON_GetObjectItem(manifest_object,"tools")) + 1);
         snprintf(trigger,sizeof(trigger),"%s:开始",cJSON_GetArrayItem(cJSON_GetObjectItem(manifest_object,"triggers"),0)->valuestring);
         assert(bk7258_runtime_skills_begin(1,trigger) == -EACCES);
         cJSON_Delete(state); cJSON_Delete(manifest_object);
       }
     return 0;
   }
 if (argc == 2 && !strcmp(argv[1], "--restart"))
   {
     assert(bk7258_runtime_skills_init() == 0);
     assert(bk7258_runtime_skills_begin(1,"定制助手:开始") == 1);
     return 0;
   }
 (void)unlink("/tmp/bk7258-runtime-skills/config.bin");
 (void)unlink("/tmp/bk7258-runtime-skills/config.pending");
 (void)rmdir("/tmp/bk7258-runtime-skills/config.pending");
 (void)rmdir("/tmp/bk7258-runtime-skills");
 assert(bk7258_runtime_skills_init() == 0);
 assert(bk7258_runtime_skills_begin(1,"专注助手：开始") == 1);
 assert(bk7258_runtime_skills_begin(1,"识物解说：这个") == 1);
 assert(bk7258_runtime_skills_check(1,"focus_timer","{\"action\":\"start\",\"seconds\":60}") == 0);
 assert(bk7258_runtime_skills_check(1,"reply_expression","{\"emotion\":\"thinking\"}") == 0);
 assert(bk7258_runtime_skills_check(1,"reply_expression","{\"emotion\":\"happy\",\"extra\":1}") == -EINVAL);
 assert(bk7258_runtime_skills_check(1,"focus_timer","{\"action\":\"start\",\"seconds\":0}") == -EINVAL);
 assert(bk7258_runtime_skills_check(1,"device_motion","{}") == -EACCES);
 assert(bk7258_runtime_skills_check(2,"focus_timer","{\"action\":\"status\"}") == -ESTALE);
 assert(bk7258_runtime_skills_prompt(1,prompt,sizeof(prompt)) == 0 && strstr(prompt,"focus-coach"));
 assert(strstr(prompt,"seconds") && strstr(prompt,"status"));
 assert(bk7258_runtime_skills_check(1,"read_file","{\"path\":\"/data/agent/skills/focus-coach.md\"}") == 0);
 assert(bk7258_runtime_skills_check(1,"read_file","{\"path\":\"/data/agent/skills/object-guide.md\"}") == -EACCES);
 assert(bk7258_runtime_skills_check(1,"read_file","{\"path\":\"/data/agent/skills/focus-coach.md\",\"extra\":1}") == -EINVAL);
 assert(bk7258_runtime_skills_read_file(1,"/data/agent/skills/focus-coach.md",&doc) == 0 && strstr(doc,"focus"));
 assert(bk7258_runtime_skills_read_file(1,"/data/agent/skills/../focus-coach",&doc) == -EACCES);
 bk7258_runtime_skills_end(1); assert(bk7258_runtime_skills_check(1,"focus_timer","{\"action\":\"status\"}") == -ESTALE);
 assert(bk7258_runtime_skills_begin(2,"设备体检:现在") == 1);
 assert(bk7258_runtime_skills_check(2,"device_status","{}") == 0);
 assert(bk7258_runtime_skills_check(2,"device_motion","{}") == 0);
 assert(bk7258_runtime_skills_check(2,"camera_capture","{}") == -EACCES);
 bk7258_runtime_skills_end(2);
 assert(bk7258_runtime_skills_begin(3,"识物解说：这个") == 1);
 assert(bk7258_runtime_skills_check(3,"camera_capture","{\"prompt\":\"describe object\"}") == 0);
 assert(bk7258_runtime_skills_check(3,"camera_capture","{}") == -EINVAL);
 assert(bk7258_runtime_skills_check(3,"device_status","{}") == 0);
 assert(bk7258_runtime_skills_enable("object-guide",false) == 0);
 assert(bk7258_runtime_skills_begin(4,"识物解说:这个") == -EACCES);
 assert(bk7258_runtime_skills_begin(5,"普通问题") == 0);
 assert(bk7258_runtime_skills_begin(5,"专注助手：开始") == 0);
 bk7258_runtime_skills_end(4);
 assert(bk7258_runtime_skills_begin(5,"普通问题") == 0);
 bk7258_runtime_skills_end(5);
 assert(bk7258_runtime_skills_begin(5,"普通问题") == -ESTALE);
 /* no configured key, malformed/tampered/unsigned external content fails */
 assert(bk7258_runtime_skills_install((const uint8_t*)"{}",2,(const uint8_t*)"x",1) == -EKEYREJECTED);
 { char key64[160], manifest64[512], sig64[128], request[1024], read[2048];
   char *revision; size_t n;
   assert(bk7258_runtime_skills_app_read(read,sizeof(read)) == 0);
   assert(bk7258_runtime_skills_app_apply("{\"op\":\"unknown\"}",read,sizeof(read)) == -EINVAL);
   assert(bk7258_runtime_skills_app_apply("{\"revision\":\"1\",\"op\":\"trust\",\"public_key\":\"!\"}",read,sizeof(read)) == -EINVAL);
   assert(bk7258_runtime_skills_app_apply("{\"revision\":\"1\",\"op\":\"trust\",\"public_key\":\"x\",\"extra\":true}",read,sizeof(read)) == -EINVAL);
   revision = strstr(read,"\"revision\":\""); assert(revision); revision += 12;
   assert(mbedtls_base64_encode((unsigned char *)key64,sizeof(key64),&n,key,sizeof(key)) == 0); key64[n] = 0;
   snprintf(request,sizeof(request),"{\"revision\":\"%.*s\",\"op\":\"trust\",\"public_key\":\"%s\"}", (int)strcspn(revision,"\""),revision,key64);
   { int install_ret = bk7258_runtime_skills_app_apply(request,read,sizeof(read));
     if (install_ret) fprintf(stderr, "install_ret=%d\n", install_ret);
     assert(install_ret == 0); }
   assert(mbedtls_base64_encode((unsigned char *)manifest64,sizeof(manifest64),&n,(const unsigned char *)manifest,strlen(manifest)) == 0); manifest64[n] = 0;
   assert(mbedtls_base64_encode((unsigned char *)sig64,sizeof(sig64),&n,signature,sizeof(signature)) == 0); sig64[n] = 0;
   revision = strstr(read,"\"revision\":\""); assert(revision); revision += 12;
   snprintf(request,sizeof(request),"{\"revision\":\"%.*s\",\"op\":\"install\",\"manifest\":\"%s\",\"signature\":\"%s\"}",(int)strcspn(revision,"\""),revision,manifest64,sig64);
   { char saved = manifest64[0]; manifest64[0] = saved == 'A' ? 'B' : 'A';
   snprintf(request,sizeof(request),"{\"revision\":\"%.*s\",\"op\":\"install\",\"manifest\":\"%s\",\"signature\":\"%s\"}",(int)strcspn(revision,"\""),revision,manifest64,sig64);
   assert(bk7258_runtime_skills_app_apply(request,read,sizeof(read)) == -EKEYREJECTED);
   manifest64[0] = saved; }
   snprintf(request,sizeof(request),"{\"revision\":\"%.*s\",\"op\":\"install\",\"manifest\":\"%s\",\"signature\":\"%s\"}",(int)strcspn(revision,"\""),revision,manifest64,sig64);
   assert(bk7258_runtime_skills_app_apply(request,read,sizeof(read)) == 0);
   assert(strstr(read,"\"custom\"") && strstr(read,"\"enabled\":false"));
   assert(bk7258_runtime_skills_begin(6,"定制助手:开始") == -EACCES);
   revision = strstr(read,"\"revision\":\""); assert(revision); revision += 12;
   snprintf(request,sizeof(request),"{\"revision\":\"%.*s\",\"id\":\"custom\",\"enabled\":true}",(int)strcspn(revision,"\""),revision);
   assert(bk7258_runtime_skills_app_apply(request,read,sizeof(read)) == 0);
   assert(bk7258_runtime_skills_begin(7,"定制助手:开始") == 1);
   assert(bk7258_runtime_skills_prompt(7,prompt,sizeof(prompt)) == 0);
   assert(strstr(prompt,"# custom") && !strstr(prompt,"device_status") && !strstr(prompt,"camera_capture"));
   assert(bk7258_runtime_skills_check(7,"focus_timer","{\"action\":\"status\"}") == 0);
   assert(bk7258_runtime_skills_check(7,"camera_capture","{\"prompt\":\"x\"}") == -EACCES);
   assert(bk7258_runtime_skills_app_apply("{\"revision\":\"0\",\"id\":\"custom\",\"enabled\":false}",read,sizeof(read)) == -EBUSY);
   bk7258_runtime_skills_end(7);
   assert(bk7258_runtime_skills_app_apply("{\"revision\":\"0\",\"id\":\"custom\",\"enabled\":false}",read,sizeof(read)) == -EAGAIN);
   revision = strstr(read,"\"revision\":\""); assert(revision); revision += 12;
   snprintf(request,sizeof(request),"{\"revision\":\"%.*s\",\"id\":\"custom\",\"enabled\":false}",(int)strcspn(revision,"\""),revision);
   g_commit_failure = -EIO;
   assert(bk7258_runtime_skills_app_apply(request,read,sizeof(read)) == -EIO);
   g_commit_failure = 0;
   assert(bk7258_runtime_skills_app_read(read,sizeof(read)) == 0 && strstr(read,"\"enabled\":true"));
   revision = strstr(read,"\"revision\":\""); assert(revision); revision += 12;
   snprintf(request,sizeof(request),"{\"revision\":\"%.*s\",\"id\":\"custom\",\"enabled\":false}",(int)strcspn(revision,"\""),revision);
   g_commit_failure = -EINPROGRESS;
   assert(bk7258_runtime_skills_app_apply(request,read,sizeof(read)) == -EINPROGRESS);
   g_commit_failure = 0;
   assert(bk7258_runtime_skills_app_apply(request,read,sizeof(read)) == -EINPROGRESS);
   assert(bk7258_runtime_skills_init() == 0);
   assert(bk7258_runtime_skills_begin(8,"定制助手:开始") == 1);
   bk7258_runtime_skills_end(8);
   { pid_t child = fork(); int status;
     assert(child >= 0);
     if (child == 0) { execl(argv[0], argv[0], "--restart", (char *)NULL); _exit(127); }
     assert(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0); }
 }
 return 0;
}
