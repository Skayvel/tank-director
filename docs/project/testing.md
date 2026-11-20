# 测试

四层，从便宜到昂贵。**上一层的通过不能替代下一层的通过。**

## 第 1 层：静态检查（无游戏）

| 检查 | 命令/位置 | 通过标准 |
|---|---|---|
| 项目一致性 | `tools/verify_project.py` | 构建哈希、证据引用、条件双后继全部通过 |
| 控制边登记 | `tools/audit_cfg.py` | 注释集合与已观察条件集合一致 |
| 源码解析 | 人工核对 | 关键常量、参数与文档一致 |
| 链接检查 | 见 [链接校验](#附录-链接校验) | 无死链 |

**这些是证据检查，不是游戏测试。**

## 第 2 层：编译

```powershell
& $spcomp .\scripting\tankdirector_menu.sp "-i$inc" '-o.\build\out.smx' '-E'
```

**通过标准**：退出码 0，0 error / 0 warning。

**它证明了什么**：编译器接受源码。
**它没证明什么**：服务器能加载、菜单显示正确、行为符合预期。

## 第 3 层：隔离服 / 本地服

不涉及真实玩家，验证「能不能跑」。

| 项 | 方法 | 通过标准 |
|---|---|---|
| 加载 | `sm plugins load tankdirector_menu` | 出现在 `sm plugins list`，版本正确 |
| 依赖 | `sm_tank_status` | 无 `警告：` 行 |
| 菜单 | `!tank` 逐级进入 | 11 个分节可打开、可返回 |
| 参数写入 | `sm_tank_set` + `sm_tank_params` | 读回一致 |
| 脚本桥 | `sm_tank_status` 脚本行 | `已连接 Tank Run 模式脚本` |
| 生成 | `sm_tank_spawn native` | 生成成功且计入状态 |
| 诊断 | `sm_tank_dump` | 文件生成、内容分块完整 |
| 清理 | `sm_tank_clear` | 全部 AI Tank 消失 |

历史样本（`build/tankrun_capacity_runtime.log`）：隔离服 `c2m2_fairgrounds` 同时跑到 8 席位 + 22 原版 + 8 补位，读回/追击/射线/扣血/拳伤检查通过。

## 第 4 层：实服玩家验证

见 [验收剧本](../user-guide/validation-playbook.md)。这是**唯一**能把结论从「待实测」升级为「已验证」的层。

必须记录：

1. 构建（`server.dll` 短哈希）
2. 地图 / 难度 / 真人数量 / tickrate
3. 本轮回答的问题
4. 结论与适用条件
5. 证据文件 + 行号或时间
6. 反例或未解项
7. 下一步

## 四层与结论状态的关系

| 完成到 | 可以声明 |
|---|---|
| 第 1 层 | `CONFIRMED`（仅限静态命题） |
| 第 2 层 | 「编译通过」 |
| 第 3 层 | 「本地/隔离服加载与基本功能通过」 |
| 第 4 层 | 才可能写 `RUNTIME_VALIDATED`——且必须绑定版本、场景、输入 |

**当前项目：第 4 层的函数级验证数为 0。**

## 常见错误

| 错误 | 为什么错 |
|---|---|
| 「编译通过 = 功能完成」 | 编译只证明语法与类型 |
| 「我本地能看到 Tank = 生成逻辑正确」 | 没对照就说不出是插件还是原版脚本生成的 |
| 「日志里有释放 = 首石成功」 | 必须确认是同一次尝试、同一个 rock |
| 「10 分钟没出问题 = 稳定」 | 样本量不足以下结论 |
| 「多人测试过 = 支持多人」 | 要写清人数、槽位配置、是否真实客户端 |

## 附录：链接校验

仓库内的 Markdown 相对链接可批量检查（PowerShell）：

```powershell
$root = (Resolve-Path '.').Path
Get-ChildItem -Recurse -Filter *.md | ForEach-Object {
    $file = $_
    $text = Get-Content -LiteralPath $file.FullName -Raw
    [regex]::Matches($text, '\]\((?<u>[^)]+)\)') | ForEach-Object {
        $u = $_.Groups['u'].Value
        if ($u -match '^(https?:|mailto:|#)') { return }
        $u = ($u -split '#')[0]
        if (-not $u) { return }
        $target = Join-Path $file.DirectoryName $u
        if (-not (Test-Path -LiteralPath $target)) {
            "{0}: {1}" -f $file.FullName.Substring($root.Length+1), $u
        }
    }
}
```

历史上已知会「失败」的三类链接，属于预期：

1. 指向研究仓库的引用（`analysis/`、`evidence/`、`indexes/`）——公开发行版不包含这些目录；
2. 指向仓库外路径的引用；
3. 指向 `build/` 内本地构建产物的引用（`build/` 已被 `.gitignore` 排除）。

公开发行版本体应当做到 **0 条无法解析的相对链接**。复核方式：

```powershell
.\tools\check-links.ps1 -Root . -Detail -FailOnUnresolved
```

该脚本的分类互斥，四个桶相加等于总数，输出末尾会打印两行自检等式。当前结果见 [公开发布检查报告](../audit/public-release-report.md) 第 5 节。
