/*
 * 默认 keymap.json —— 来自 OpenMicroKbd 的 DEFAULT_KEYMAP（协议参考实现）。
 * 仅供 Gate 0A 让 Input 应用能画出键盘；固件并不解析/执行这套键位（不实现图层引擎）。
 * 由 _gen_keymap_header.py 生成，勿手改。
 */
#define KEYMAP_JSON_LEN 606

static const char KEYMAP_JSON[] =
    "{\"version\":1,\"activeProfileId\":0,\"profiles\":[{\"id\":0,\"name\":\"Codex\",\"layers\":[{\"id\":0,\"name\":\"ChatGPT\",\"color\":16711680,\"os\":0,\"layout\":{\"keymap\":[[\"KV_OAI_AG00\",\"KV_OAI_AG01\"],[\"KV_OAI_AG02\",\"KV_OAI_AG03\",\"KV_OAI_AG04\",\"KV_OAI_AG05\"],[\"KV_OAI_ACT06\",\"KV_OAI_ACT07\",\"KV_OAI_ACT08\",\"KV_OAI_ACT09\"],[\"KV_OAI_ACT10\",\"KV_OAI_ACT11\",\"KV_OAI_ACT12\"]],\"encoders\":[[\"KV_OAI_ENC_CC\",\"KV_OAI_ENC_CW\",\"KV_OAI_ENC_CLK\"]],\"buttons\":[[\"KC_MPLY\"]],\"joystick\":{\"type\":\"VENDOR\",\"sectors\":[]}}}],\"macrosUsed\":[],\"multiActionsUsed\":[]}],\"multiActions\":[],\"macros\":[],\"macrosGroups\":[],\"multiActionsGroups\":[],\"linkedApps\":[]}";

static const char KEYMAP_SHA1[] = "4a75f8fef9ab827dc71f8c33b001d97c4325751d";

static const char KEYMAP_BASE64[] =
    "eyJ2ZXJzaW9uIjoxLCJhY3RpdmVQcm9maWxlSWQiOjAsInByb2ZpbGVzIjpbeyJpZCI6MCwibmFtZSI6IkNvZGV4IiwibGF5ZXJzIjpbeyJpZCI6MCwibmFtZSI6IkNoYXRHUFQiLCJjb2xvciI6MTY3MTE2ODAsIm9zIjowLCJsYXlvdXQiOnsia2V5bWFwIjpbWyJLVl9PQUlfQUcwMCIsIktWX09BSV9BRzAxIl0sWyJLVl9PQUlfQUcwMiIsIktWX09BSV9BRzAzIiwiS1ZfT0FJX0FHMDQiLCJLVl9PQUlfQUcwNSJdLFsiS1ZfT0FJX0FDVDA2IiwiS1ZfT0FJX0FDVDA3IiwiS1ZfT0FJX0FDVDA4IiwiS1ZfT0FJX0FDVDA5Il0sWyJLVl9PQUlfQUNUMTAiLCJLVl9PQUlfQUNUMTEiLCJLVl9PQUlfQUNUMTIiXV0sImVuY29kZXJzIjpbWyJLVl9PQUlfRU5DX0NDIiwiS1ZfT0FJX0VOQ19DVyIsIktWX09BSV9FTkNfQ0xLIl1dLCJidXR0b25zIjpbWyJLQ19NUExZIl1dLCJqb3lzdGljayI6eyJ0eXBlIjoiVkVORE9SIiwic2VjdG9ycyI6W119fX1dLCJtYWNyb3NVc2VkIjpbXSwibXVsdGlBY3Rpb25zVXNlZCI6W119XSwibXVsdGlBY3Rpb25zIjpbXSwibWFjcm9zIjpbXSwibWFjcm9zR3JvdXBzIjpbXSwibXVsdGlBY3Rpb25zR3JvdXBzIjpbXSwibGlua2VkQXBwcyI6W119";
