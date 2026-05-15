#!/bin/bash

# Setup script for AI2T_LingXinEngine Git hooks
# 自动配置 Git hooks 并设置执行权限

echo "🔧 Setting up Git hooks for AI2T_LingXinEngine..."

# 获取脚本所在目录（.githooks 目录）
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# 获取仓库根目录（.githooks 的上级目录）
REPO_DIR="$( cd "$SCRIPT_DIR/.." && pwd )"

echo "📂 Repository directory: $REPO_DIR"

# 切换到仓库根目录
cd "$REPO_DIR"

# ==================== 配置 Git 使用 .githooks 目录 ====================
echo ""
echo "⚙️  Configuring Git to use .githooks directory..."

if git config core.hooksPath .githooks; then
    echo "  ✅ Git hooks path configured successfully"
else
    echo "  ❌ Failed to configure Git hooks path"
    exit 1
fi

# ==================== 给 hook 文件添加执行权限 ====================
echo ""
echo "🔑 Setting executable permissions for hook files..."

# 检查并设置每个 hook 文件的权限
hook_count=0

for hook_file in .githooks/pre-commit .githooks/commit-msg .githooks/pre-push; do
    if [ -f "$hook_file" ]; then
        chmod +x "$hook_file"
        echo "  ✓ $(basename $hook_file)"
        ((hook_count++))
    fi
done

# 给 setup.sh 本身也添加执行权限
if [ -f ".githooks/setup.sh" ]; then
    chmod +x .githooks/setup.sh
    echo "  ✓ setup.sh"
fi

# ==================== 完成提示 ====================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ Git hooks setup complete!"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "📋 Configuration summary:"
echo "  • Hooks directory: $(git config core.hooksPath)"
echo "  • Active hooks: $hook_count"
echo ""
echo "💡 Usage tips:"
echo "  • To skip hooks for a single commit: git commit --no-verify"
echo "  • To skip hooks for a single push: git push --no-verify"
echo "  • To disable hooks: git config core.hooksPath ''"
echo ""