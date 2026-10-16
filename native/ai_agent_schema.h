/* llama.cpp OpenAI-compatible constrained decoding, only for file-agent calls. */
static const char kAiAgentSchema[] =
  ",\"response_format\":{\"type\":\"json_schema\",\"json_schema\":{\"name\":\"cursorpad_files\",\"strict\":true,\"schema\":{"
  "\"type\":\"object\",\"properties\":{"
  "\"tool\":{\"type\":\"string\",\"enum\":[\"list\",\"find\",\"read\",\"mkdir\",\"write\",\"copy\",\"move\",\"rename\",\"open\",\"ask\",\"done\"]},"
  "\"path\":{\"type\":\"string\"},\"src\":{\"type\":\"string\"},\"dst\":{\"type\":\"string\"},"
  "\"pattern\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"},\"offset\":{\"type\":\"string\"},"
  "\"recursive\":{\"type\":\"string\",\"enum\":[\"yes\",\"no\"]}},"
  "\"required\":[\"tool\"],\"additionalProperties\":false}}}";
