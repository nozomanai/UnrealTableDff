# Table Diff

Unreal Engine 编辑器插件。在内容浏览器里右键 Data Table，选择 **TableDiff**，把当前表和 Perforce 或 Git 的某条历史记录做对比。对比窗口的布局接近蓝图 Diff。

## 用法

1. 在编辑器里启用 Perforce 或 Git 版本控制。
2. 右键一个 Data Table，打开 **TableDiff**。
3. 在历史列表里选一条记录。Perforce 显示 Changelist，Git 显示提交号。
4. 左侧是差异列表，可用 Prev / Next 跳转。右侧是表格内容。

对比的是所选历史版本和当前打开的表，包含尚未保存的修改。

## 视图

工具栏可以在两种视图之间切换：

- **Grid**：左右并排两张表，一行对一行。
- **Structure**：按数据表的行和结构体层级展开、收起，History 与 Current 两列对照。

新增为绿色，删除为红色，修改为黄色。有差异的行和包含差异的结构体会默认展开。**Changes only** 只保留有变化的行，搜索框可按行名或单元格内容过滤。

## 滚动

表格右侧是竖向滚动条，底部是横向滚动条，都可以拖动。滚轮上下滚动；按住 Shift 再滚轮则左右滚动。
