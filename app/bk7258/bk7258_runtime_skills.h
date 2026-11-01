/****************************************************************************
 * apps/bk7258/bk7258_runtime_skills.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_RUNTIME_SKILLS_H
#define __APP_BK7258_RUNTIME_SKILLS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Public Definitions and Prototypes
 ****************************************************************************/

#define BK7258_RUNTIME_SKILLS_MAX_KEY 256u

/* Loads and re-verifies the durable RuntimeSkill record before it can be
 * selected.  A failed or uncertain store remains unavailable until init is
 * retried successfully.
 */

int bk7258_runtime_skills_init(void);

/* The owner calls begin after ASR final and calls end at TURN_COMPLETE.
 */

int bk7258_runtime_skills_begin(uint64_t request, const char *text);
bool bk7258_runtime_skills_selected(uint64_t request);
void bk7258_runtime_skills_end(uint64_t request);
void bk7258_runtime_skills_clear(void);
int bk7258_runtime_skills_enable(const char *id, bool enabled);

/* Kind-27 owner operations. Revision is a decimal string. apply accepts an
 * enable patch, publisher DER trust import or exact signed package import;
 * EAGAIN means a revision mismatch. Success requires durable publication.
 */

int bk7258_runtime_skills_app_read(char *out, size_t size);
int bk7258_runtime_skills_app_apply(const char *json, char *out,
                                    size_t size);

/* Startup enumeration for the existing loader.  JSON has an `items` array;
 * each item supplies immutable `id` and `markdown` strings.
 */

int bk7258_runtime_skills_builtin_read(char *out, size_t size);

/* Call immediately before every tool invocation.  A selected skill is
 * restrictive; a miss leaves the caller's normal Agent policy unchanged.
 */

int bk7258_runtime_skills_check(uint64_t request, const char *tool,
                                const char *input);
int bk7258_runtime_skills_prompt(uint64_t request, char *out, size_t size);
const char *bk7258_runtime_skills_markdown(const char *id);
int bk7258_runtime_skills_read_file(uint64_t request, const char *path,
                                    const char **markdown);

/* Owner-provided SubjectPublicKeyInfo DER only; package-supplied roots are
 * deliberately not accepted.  `manifest` is the exact signed UTF-8 bytes.
 */

int bk7258_runtime_skills_set_owner_key(const uint8_t *der, size_t size);
int bk7258_runtime_skills_install(const uint8_t *manifest,
                                  size_t manifest_size,
                                  const uint8_t *signature,
                                  size_t signature_size);

#endif
