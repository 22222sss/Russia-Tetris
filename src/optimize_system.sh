#!/bin/bash

echo "🔧 俄罗斯方块服务器系统优化脚本"
echo "=================================="

# 检查是否以root权限运行
if [ "$EUID" -ne 0 ]; then
    echo "⚠️  请使用sudo运行此脚本以获得完整优化效果"
    echo "    sudo $0"
    exit 1
fi

echo "1. 优化文件描述符限制..."
# 备份原有配置
cp /etc/security/limits.conf /etc/security/limits.conf.backup.$(date +%Y%m%d)
cp /etc/sysctl.conf /etc/sysctl.conf.backup.$(date +%Y%m%d)

# 设置文件描述符限制
echo "# 俄罗斯方块服务器优化 - 文件描述符限制" >> /etc/security/limits.conf
echo "* soft nofile 1000000" >> /etc/security/limits.conf
echo "* hard nofile 1000000" >> /etc/security/limits.conf
echo "root soft nofile 1000000" >> /etc/security/limits.conf
echo "root hard nofile 1000000" >> /etc/security/limits.conf

echo "2. 优化网络参数..."
# 设置网络参数
cat >> /etc/sysctl.conf << 'SYSCTL_EOF'

# 俄罗斯方块服务器优化 - 网络参数
# 连接队列优化
net.core.somaxconn = 65535
net.ipv4.tcp_max_syn_backlog = 65535
net.core.netdev_max_backlog = 30000

# TCP连接优化
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_fin_timeout = 30
net.ipv4.tcp_keepalive_time = 600
net.ipv4.tcp_keepalive_intvl = 60
net.ipv4.tcp_keepalive_probes = 5

# 内存缓冲区优化
net.ipv4.tcp_rmem = 4096 87380 16777216
net.ipv4.tcp_wmem = 4096 16384 16777216
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
net.core.optmem_max = 65536

# 其他优化
net.ipv4.tcp_syncookies = 1
net.ipv4.tcp_max_tw_buckets = 2000000
net.ipv4.tcp_synack_retries = 2
net.ipv4.tcp_syn_retries = 2
SYSCTL_EOF

echo "3. 应用系统参数..."
# 立即应用sysctl设置
sysctl -p

echo "4. 设置当前会话限制..."
# 设置当前shell的文件描述符限制
ulimit -n 1000000

echo "✅ 系统优化完成！"
echo ""
echo "📋 优化摘要："
echo "   - 文件描述符限制: 1000000"
echo "   - TCP连接队列: 65535"
echo "   - TCP缓冲区大小: 优化"
echo "   - 连接复用: 启用"
echo ""
echo "💡 建议："
echo "   1. 重新登录以使文件描述符限制生效"
echo "   2. 重启系统以获得完整的优化效果"
echo "   3. 运行检查脚本验证优化: ./check_system.sh"

# 创建检查脚本
cat > check_system.sh << 'CHECK_EOF'
#!/bin/bash
echo "🔍 系统优化检查"
echo "================"
echo "1. 文件描述符限制:"
ulimit -n
echo ""
echo "2. 关键网络参数:"
sysctl net.core.somaxconn net.ipv4.tcp_max_syn_backlog net.ipv4.tcp_tw_reuse
echo ""
echo "3. 当前TCP连接统计:"
echo "   ESTABLISHED: $(ss -ant | grep ESTAB | wc -l)"
echo "   TIME-WAIT: $(ss -ant | grep TIME-WAIT | wc -l)"
echo ""
echo "4. 内存使用:"
free -h
CHECK_EOF

chmod +x check_system.sh

echo ""
echo "📁 已创建检查脚本: ./check_system.sh"
