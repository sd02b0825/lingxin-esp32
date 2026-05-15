#!/bin/bash

# 切换到测试模式的脚本
# 功能：
# 1. lingxin_chat_api_inner.h文件中打开LINGXIN_TEST宏
# 2. 在lingxin_test_runner.c文件中修改TEST_WS_IP宏的IP值
# 3. 询问是否需要走自动化压测，是则进行音量修改和请求路径修改

set -e  # 遇到错误立即退出

echo "开始切换到测试模式..."

CHAT_API_H_FILE="./src/inc/lingxin_chat_api_inner.h"
LINGXIN_TEST_RUNNER_C_FILE="./tests/lingxin_test_runner.c"
LINGXIN_COMMON_H_FILE="./src/inc/lingxin_common.h"

# 步骤1：在lingxin_chat_api_inner.h文件中打开LINGXIN_TEST宏
echo "步骤1：在lingxin_chat_api_inner.h文件中打开LINGXIN_TEST宏"
# 检查lingxin_chat_api_inner.h文件中是否已存在LINGXIN_TEST宏定义
if grep -q "^#define LINGXIN_TEST" "$CHAT_API_H_FILE"; then
    echo "✓ LINGXIN_TEST宏已经打开"
else
    # 查找有无注释掉的宏
    if grep -q "// #define LINGXIN_TEST" "$CHAT_API_H_FILE"; then
        # 使用perl命令替换注释掉的宏
        perl -i -pe 's|// #define LINGXIN_TEST|#define LINGXIN_TEST|g' "$CHAT_API_H_FILE"
        echo "✓ LINGXIN_TEST宏打开成功"
    else
        echo "✗ 在lingxin_chat_api_inner.h文件中找不到可打开的LINGXIN_TEST宏定义"
    fi
fi

# 步骤2：在lingxin_test_runner.c文件中修改TEST_WS_IP宏的IP值
echo "步骤2：在lingxin_test_runner.c文件中修改TEST_WS_IP宏的IP值"
# 询问用户是否要更改测试WebSocket IP地址
read -p "请输入测试服务器IP地址（默认为公网IP 8.154.30.254） " test_ws_ip
if [[ -z "$test_ws_ip" ]]; then
    test_ws_ip="8.154.30.254"
fi
# 使用perl命令替换现有的IP地址
perl -i -pe "s/#define TEST_WS_IP \\\"[0-9.]+\\\"/#define TEST_WS_IP \\\"$test_ws_ip\\\"/" "$LINGXIN_TEST_RUNNER_C_FILE"
echo "✓ 已更新TEST_WS_IP宏定义为: $test_ws_ip"

# 步骤3：询问是否需要走自动化压测，是则进行音量修改和请求路径修改
echo "步骤3：询问是否需要走自动化压测，是则进行音量修改和请求路径修改"
# 询问用户是否要进行自动化压测配置
read -p "是否要进行自动化压测配置？(y/n): " enable_stress_test
if [[ $enable_stress_test == "y" || $enable_stress_test == "Y" ]]; then
    # 3.1 修改音量设置
    echo "正在修改音量设置..."
    if grep -q "set_volume(40)" "$LINGXIN_TEST_RUNNER_C_FILE"; then
        perl -i -pe 's/set_volume\(40\)/set_volume(1)/' "$LINGXIN_TEST_RUNNER_C_FILE"
        echo "✓ 已将音量从40修改为1"
    else
        echo "✗ 未找到set_volume(40)，跳过音量修改"
    fi
    
    # 3.2 修改REQUEST_URL
    echo "正在修改REQUEST_URL..."
    if grep -q '#define REQUEST_URL "eagent.edu-aliyun.com"' "$LINGXIN_COMMON_H_FILE"; then
        perl -i -pe "s/#define REQUEST_URL \\\"eagent.edu-aliyun.com\\\"/#define REQUEST_URL \\\"$test_ws_ip\\\"/" "$LINGXIN_COMMON_H_FILE"
        echo "✓ 已将REQUEST_URL修改为: $test_ws_ip"
    else
        echo "✗ 未找到REQUEST_URL定义，跳过修改"
    fi
    
    # 3.3 修改WEBSOCKET_CHAT_PATH
    echo "正在修改WEBSOCKET_CHAT_PATH..."
    if grep -q '#define WEBSOCKET_CHAT_PATH "gw/ws/open/api/v1/unifiedAccess"' "$LINGXIN_COMMON_H_FILE"; then
        perl -i -pe 's/#define WEBSOCKET_CHAT_PATH "gw\/ws\/open\/api\/v1\/unifiedAccess"/#define WEBSOCKET_CHAT_PATH ""/' "$LINGXIN_COMMON_H_FILE"
        echo "✓ 已将WEBSOCKET_CHAT_PATH修改为空字符串"
    else
        echo "✗ 未找到WEBSOCKET_CHAT_PATH定义，跳过修改"
    fi
    
    echo "✓ 自动化压测配置完成！"
else
    echo "跳过自动化压测配置"
fi


echo "✓ 测试模式切换完成！"
echo ""
echo "注意事项："
echo "- 脚本已自动在lingxin_chat_api_inner.h文件中打开LINGXIN_TEST宏"
echo "- 脚本已自动在lingxin_test_runner.c文件中修改TEST_WS_IP宏的IP值"
if [[ $enable_stress_test == "y" || $enable_stress_test == "Y" ]]; then
    echo "- 脚本已自动进行自动化压测配置，已修改音量设置和请求路径"
fi
echo "- 请重新编译项目以使更改生效"
echo "- 要切换回普通模式，可以运行switch_to_normal_mode.sh脚本"