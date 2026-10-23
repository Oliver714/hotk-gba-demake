# 存档格式

实现以 src/save.cpp 和 include/model.h 为准。

- 128 KiB FLASH，8 个槽位，每槽 16 KiB。
- 当前普通记录版本为 17；版本 1 在当前实现中也用于带解锁信息的记录形式，并通过数据长度区分。
- 当前记录头包含 magic、version、payload bytes、sequence 和 checksum，后接 Campaign。
- FNV 风格校验用于检测载荷损坏；不能代替事务性断电保证。
- 历史记录兼容逻辑位于 save.cpp，修改 Campaign 布局时必须审查版本转换及尺寸断言。

修改存档相关代码前备份 .sav，并验证旧记录加载、新记录保存、8 个槽位与解锁标记。不要随意更改结构成员顺序或复用旧版本号。主机测试中的日期/模型检查不等于运行过真实 FLASH 写入。
