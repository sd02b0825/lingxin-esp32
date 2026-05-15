#### 非芯片适配场景，AI2T_LingXinEngine 下所有代码不可修改

### 目录结构

.
├── inc/ # 外部头文件（供开发者调用）
│ ├── func/ # 可直接调用的功能模块头文件
│ └── system_adapter/ # 功能适配层头文件(（需要开发者适配实现）)
│
├── src/ # SDK 源码实现
│ ├── asr/ # ASR 实现
│ ├── inc/ # 内部使用的头文件
│ ├── llm/ # LLM 功能实现(生文、生图)
│ ├── websocket/ # websocket hook
│ ├── schedule/ # 定时任务功能实现
│ ├── tts/ # TTS 实现
│ ├── utils/ # 工具类方法
│ ├── log/ # 日志方法实现
│ └── voice_chat/ # chat 功能核心实现
└── README.md # 项目说明文档
