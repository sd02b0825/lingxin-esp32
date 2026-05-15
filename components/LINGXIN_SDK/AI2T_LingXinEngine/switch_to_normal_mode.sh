#!/bin/bash

# 切换到普通模式的脚本
# 功能：
# 1. 在lingxin_chat_api_inner.h文件中关闭LINGXIN_TEST宏
# 2. 在lingxin_test_runner.c文件中还原TEST_WS_IP宏的IP值
# 3. 检查是否有自动化压测配置，有则还原音量设置和请求路径

set -e  # 遇到错误立即退出

echo "开始切换到普通模式..."

CHAT_API_H_FILE="./src/inc/lingxin_chat_api_inner.h"
LINGXIN_TEST_RUNNER_C_FILE="./tests/lingxin_test_runner.c"
LINGXIN_COMMON_H_FILE="./src/inc/lingxin_common.h"

# 步骤1：在lingxin_chat_api_inner.h文件中关闭LINGXIN_TEST宏
echo "步骤1：在lingxin_chat_api_inner.h文件中关闭LINGXIN_TEST宏"
# 检查lingxin_chat_api_inner.h文件中是否已存在LINGXIN_TEST宏定义
if grep -q "// #define LINGXIN_TEST" "$CHAT_API_H_FILE"; then
    echo "✓ LINGXIN_TEST宏已经关闭"
else
    # 查找有无打开的宏
    if grep -q "^#define LINGXIN_TEST" "$CHAT_API_H_FILE"; then
        # 使用perl命令关闭LINGXIN_TEST宏
        perl -i -pe 's|#define LINGXIN_TEST|// #define LINGXIN_TEST|g' "$CHAT_API_H_FILE"
        echo "✓ LINGXIN_TEST宏关闭成功"
    else
        echo "✗ 在lingxin_chat_api_inner.h文件中找不到打开的LINGXIN_TEST宏定义"
    fi
fi

# 步骤2：在lingxin_test_runner.c文件中还原TEST_WS_IP宏的IP值
echo "步骤2：在lingxin_test_runner.c文件中还原TEST_WS_IP宏的IP值"
# 使用perl命令把现有的IP地址还原为公网IP
perl -i -pe "s/#define TEST_WS_IP \\\"[0-9.]+\\\"/#define TEST_WS_IP \\\"8.154.30.254\\\"/" "$LINGXIN_TEST_RUNNER_C_FILE"

# 步骤3：检查是否有自动化压测配置，有则还原音量设置和请求路径
echo "步骤3：检查是否有自动化压测配置，有则还原音量设置和请求路径"
# 3.1 检查音量设置
echo "正在检查音量设置..."
if grep -q "set_volume(1)" "$LINGXIN_TEST_RUNNER_C_FILE"; then
    perl -i -pe 's/set_volume\(1\)/set_volume(40)/' "$LINGXIN_TEST_RUNNER_C_FILE"
    echo "✓ 已将音量从1还原为40"
else
    echo "✗ 音量无需还原，跳过"
fi

# 3.2 检查REQUEST_URL
echo "正在修改REQUEST_URL..."
if grep -q '#define REQUEST_URL "eagent.edu-aliyun.com"' "$LINGXIN_COMMON_H_FILE"; then
    echo "✗ REQUEST_URL定义无需还原，跳过"
else
    perl -i -pe "s/#define REQUEST_URL \\\"[0-9.]+\\\"/#define REQUEST_URL \\\"eagent.edu-aliyun.com\\\"/" "$LINGXIN_COMMON_H_FILE"
    echo "✓ 已将REQUEST_URL还原为eagent.edu-aliyun.com"
fi

# 3.3 检查WEBSOCKET_CHAT_PATH
echo "正在修改WEBSOCKET_CHAT_PATH..."
if grep -q '#define WEBSOCKET_CHAT_PATH "gw/ws/open/api/v1/unifiedAccess"' "$LINGXIN_COMMON_H_FILE"; then
    echo "✗ WEBSOCKET_CHAT_PATH定义无需还原，跳过"
else
    perl -i -pe 's/#define WEBSOCKET_CHAT_PATH ""/#define WEBSOCKET_CHAT_PATH "gw\/ws\/open\/api\/v1\/unifiedAccess"/' "$LINGXIN_COMMON_H_FILE"
    echo "✓ 已将WEBSOCKET_CHAT_PATH还原为gw/ws/open/api/v1/unifiedAccess"
fi

echo "✓ 自动化压测配置检查还原完成！"


echo "✓ 普通模式切换完成！"
echo ""
echo "注意事项："
echo "- 脚本已自动在lingxin_chat_api_inner.h文件中关闭LINGXIN_TEST宏"
echo "- 脚本已自动在lingxin_test_runner.c文件中还原TEST_WS_IP宏的IP值"
echo "- 脚本已自动进行自动化压测配置检查和还原"
echo "- 请重新编译项目以使更改生效"
echo "- 要切换回测试模式，可以运行switch_to_test_mode.sh脚本"