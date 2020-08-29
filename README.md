# Ventoy简介
Ventoy是一个制作可启动U盘的开源工具。有了Ventoy你就无需反复地格式化U盘，你只需要把ISO/WIM/IMG/EFI文件拷贝到U盘里面就可以启动了，无需其他操作。 
你可以一次性拷贝很多个不同类型的ISO文件，在启动时Ventoy会显示一个菜单来选择。 
无差异支持Legacy BIOS和UEFI模式。目前已经测试了各类超过470+ 个ISO文件(列表). 
同时提出了"Ventoy Compatible"的概念，若被支持则理论上可以启动任何ISO文件.
项目官网: [https://www.ventoy.net](https://www.ventoy.net)

# 特点
- 100% 开源 (许可证)
- 使用简单 (使用说明)
- 快速 (拷贝文件有多快就有多快)
- 直接从 ISO/WIM/IMG/EFI 文件启动，无需解开
- 无差异支持Legacy + UEFI 模式
- UEFI 模式支持安全启动 (Secure Boot) (1.0.07版本开始) 说明
- 支持持久化 (1.0.11版本开始) 说明
- 支持直接启动WIM文件(Legacy + UEFI) (1.0.12+) 说明
- 支持MBR和GPT分区格式(1.0.15+)
- 支持自动安装部署(1.0.09+) 说明
- 支持超过4GB的ISO文件
- 保留ISO原始的启动菜单风格(Legacy & UEFI)
- 支持大部分常见操作系统, 已测试470+ 个ISO文件
- 不仅仅是启动，而是完整的安装过程
- ISO文件支持列表模式或目录树模式显示 说明
- 提出 "Ventoy Compatible" 概念
- 支持插件扩展
- 启动过程中支持U盘设置写保护
- 不影响U盘日常普通使用
- 版本升级时数据不会丢失
- 无需跟随操作系统升级而升级Ventoy

