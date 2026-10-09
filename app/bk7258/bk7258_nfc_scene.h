/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_NFC_SCENE_H
#define BK7258_NFC_SCENE_H
#include "bk7258_nfc_bindings.h"
/* 单一采样所有者串行调用；不持有UID、不扫描、不访问文件、不授予权限。
 * generation在所有者生命周期内固定，sequence严格递增且不允许回绕。
 * UNKNOWN不能证明移开；只有完整可信的ABSENT观察才重新允许一次触发。
 * 首次PRESENT消耗资格，包括未知卡、忙碌或关闭准入；不延后重放。
 */
enum bknfc_scene_sample_e
{
  BKNFC_SCENE_UNKNOWN = 0,
  BKNFC_SCENE_ABSENT = 1,
  BKNFC_SCENE_PRESENT = 2
};
struct bknfc_scene_s
{
  uint64_t generation;
  uint64_t sequence;
  bool armed;
};
/* 仅首次建立生命周期时调用，不能用重置此对象绕过驻留去重。 */
int bknfc_scene_init(struct bknfc_scene_s *state, uint64_t generation);
/* 返回0且intent!=0仅表示共同专注服务受理，必须查询该intent才能确认应用。
 * 输入错误不消耗序号；旧会话/重复序号拒绝。关闭准入不取消已应用计时。
 * 采样失败传UNKNOWN而不是ABSENT；此接口不定义RF移开判据或轮询周期。
 */
int bknfc_scene_observe(struct bknfc_scene_s *state,
                         const struct bknfc_bindings_s *bindings,
                         uint64_t generation, uint64_t sequence,
                         unsigned int kind, const struct bknfc_card_s *card,
                         bool admitted, uint32_t *intent);
#endif
