# GitHub Actions 与发布

工作流：.github/workflows/build.yml。

1. 普通 main/master 推送、PR 和手动触发：安装固定工具链、运行主机回归测试、构建时自动下载固定 Butano、编译并检查 ROM，上传 Actions 构建附件。
2. 推送 v* 标签：先完成同样的构建，再由独立发布任务创建或更新对应 Release，上传 ROM、ZIP 和 SHA256SUMS.txt。
3. 仅发布任务拥有 contents: write；构建任务只读。使用 GITHUB_TOKEN，不需要个人访问令牌。Actions 引用固定提交 SHA。

示例：

```bash
git tag v0.1.0
git push origin v0.1.0
```

使用 GitHub 仓库设置允许 Actions 运行；组织级策略也必须允许发布任务请求写权限。普通手动运行只生成构建附件，不发布 Release。标签应由维护者创建。

工具链安装包逐项固定 URL、版本和 SHA256。安装器不会静默升级；如果滚动仓库未来删除旧包，构建会失败，应按明确的版本升级流程更新锁文件并回归测试，而不是放松校验。依赖下载和 GitHub runner 本身仍受外部环境影响，因此不承诺永远逐字节复现。

本地可运行 `python3 tools/package_release.py` 检查发布附件。ZIP 带代码协议、第三方说明及素材状态清单；ELF 和缓存不作为公开附件，完整源码由同标签的仓库提供。

发布前更新 CHANGELOG.md，核实素材许可状态，并完成模拟器检查。GitHub 原生源码压缩包包含该标签下的已提交生成资源与构建脚本。

Butano 不再随仓库提交。Actions 调用 build.sh 自动安装依赖；发布 ZIP 中仍保留下载所得的 Butano 许可证。
