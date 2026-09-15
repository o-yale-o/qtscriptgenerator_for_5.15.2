# Qt Script Generator (labs package, version 0.2)

The Qt Script Generator is a tool that generates Qt bindings for Qt Script. This should work
with Qt 5.6.0 and MSVC 2015 (Update 1).

---

## Instructions

1. **Build the generator**

   ```
   cd path/to/this/project/generator
   qmake && make
   ```

2. **Run the generator** (without arguments)

   This will generate C++ files in `path/to/this/project/generated_cpp`
   and documentation in `path/to/this/project/doc`.

3. **Build the bindings plugins**

   ```
   cd path/to/this/project/qtbindings
   qmake && make
   ```

   The plugins will be put under `path/to/this/project/plugins`.

4. **Use the plugins in your application**

   Add the plugins path to the library paths
   (`QCoreApplication::setLibraryPaths()`), then call `QScriptEngine::importExtension()`
   (plugin keys are `"qt.core"`, `"qt.gui"`, etc).

There is a simple script interpreter / launcher in `path/to/this/project/qtbindings/qs_eval`
that imports all the bindings. You can use it to run the examples found in
`path/to/this/project/examples`. E.g., with the examples directory being the working directory:

```
../qtbindings/qs_eval/qs_eval CollidingMice.js
```

See the generated `doc/index.html` for more information.

Have fun!

---

## 【修改说明】

本仓库基于原版 Qt Script Generator 0.2 修改，使其能在 Qt 5.15.2（MSVC 2019）下正确生成绑定代码。

### 背景

原版生成器只针对 Qt 4 时代的 C++ 语法。在 Qt 5.15.2 头文件中，大量类（QObjectData、QIODevice 等）
内部使用了 C++11 的 `= default` / `= delete` 函数定义语法，例如（qobject.h）：

```cpp
class QObjectData {
    QObjectData(const QObjectData &) = delete;
    QObjectData &operator=(const QObjectData &) = delete;
public:
    QObjectData() = default;
    virtual ~QObjectData() = 0;
    ...
};

class Q_CORE_EXPORT QObject { ... };   // 紧跟在 QObjectData 之后
```

旧 parser 的 `parseInitializer()` 只识别 `= <表达式>` 和 `= 0`，遇到 `= default` / `= delete`
时，关键字既不是表达式也不是 0，子解析器会失败但不会消费 token，导致 parser 状态机错乱，
从而把紧随其后的整个类（如 QObject 本身）跳过。QObject 一旦丢失，依赖链上的 QFile /
QFileDevice / QIODevice 等大量类会在拓扑排序阶段被一并丢弃，最终每次只生成 5~10 个
类型（且不稳定，因 QHash 迭代顺序随机）。

排查时表现为：typesystem 中 71 个 core 类，实际只生成个位数；表面看似"头文件多行注释
/某种新语法把后续类吃掉了"。

### 修复

- 文件：`generator/parser/parser.cpp`
- 函数：`Parser::parseInitializer`（约第 2187 行）

在消费完 `=` 之后，先判断下一个 token 是否为 `Token_default` 或 `Token_delete`：
若是，则直接消费该关键字并构造一个空的 `InitializerClause`，不再走表达式解析路径。
否则维持原有逻辑（按表达式或 `= 0` 处理）。

```cpp
if (nextTk == Token_default || nextTk == Token_delete)
{
    token_stream.nextToken();
    InitializerClauseAST *clause = CreateNode<InitializerClauseAST>(_M_pool);
    ast->initializer_clause = clause;
}
else if (!parseInitializerClause(ast->initializer_clause))
{
    reportError(("Initializer clause expected"));
}
```

### 其它配套修改（main.cpp / main.h）

为脱离 Qt 资源系统 (.qrc) 在文件系统上运行，`generator/main.cpp` 与 `generator/main.h`
已改为从 `applicationDirPath()/data/` 下读取 `qtscript_masterinclude.h`、`build_all.txt`
和 `pp-qt-configuration`；预处理器仍从环境变量 `INCLUDE`、`QTDIR` 以及命令行
`--include-paths` 获取头文件搜索路径。

### 验证

环境：Qt 5.15.2 (msvc2019_64) + MSVC 2019。

修复后重新生成所有模块，cpp 文件数量与原 4 月份正常运行的输出一致（gui 反而多 1 个）：

```
core=70  gui=86  widgets=189  network=17  xml=17  xmlpatterns=18
multimedia=2  opengl=5  printsupport=7  sql=12  svg=3  uitools=1
```

用 cl.exe 实际编译 qtscript_QAbstractAnimation.cpp / QFile.cpp / QFileDevice.cpp /
QIODevice.cpp / QProcess.cpp / QDate.cpp / QSettings.cpp 全部通过（仅 Qt 自身的
deprecated API 警告，无错误）。

### 运行步骤（Qt 5.15.2 / Windows）

1. **生成 generator.exe**（已在 `generator/release/` 和 `generator/buil-result/` 提供）：

   ```
   cd generator
   qmake generator.pro -spec win32-msvc "CONFIG += release"
   nmake /f Makefile.Release
   ```

2. **设置环境并运行生成**（注意必须设 QTDIR 并把 `Qt\5.15.2\msvc2019_64\bin` 加入 PATH，
   否则 Qt5Core.dll 加载失败、预处理器也找不到 Qt 头文件）：

   ```
   set QTDIR=C:\Qt\5.15.2\msvc2019_64
   set PATH=C:\Qt\5.15.2\msvc2019_64\bin;%PATH%
   cd generator
   generator.exe qtscript_masterinclude.h typesystem_core.xml
   generator.exe qtscript_masterinclude.h typesystem_gui.xml
   generator.exe qtscript_masterinclude.h typesystem_widgets.xml
   ... 其余模块 (network / xml / sql / svg / opengl / multimedia / printsupport /
       xmlpatterns / uitools) 同理。
   ```

3. **编译绑定插件**：

   ```
   cd qtbindings
   qmake qtbindings.pro -spec win32-msvc "CONFIG += release"
   nmake
   ```

### 生成脚本解释器 qs_eval

`qtbindings/qs_eval/qs_eval.exe` 是一个简单的脚本解释器/启动器：它会导入全部绑定插件
（`qt.core`、`qt.gui`、`qt.widgets` 等），因此脚本里可以直接 `new QPushButton()` 使用 Qt 类。
**无需单独编译**——它已列入 `qtbindings.pro` 的 `SUBDIRS`，上一步编译绑定插件时会一起生成，
产物位于 `qtbindings/qs_eval/qs_eval.exe`（依赖同目录下的 Qt DLL 及 `plugins/` 下的绑定插件，
需保证运行时 `QTDIR\...\bin` 在 PATH 中，且插件的搜索路径可达）。

### 跑 Demo（验证脚本能正常引用 Qt 类）

绑定编译完成后，用 qs_eval 运行 `examples/` 下自带的示例脚本做最终验证
（以 examples 目录为工作目录）：

```
cd examples
..\qtbindings\qs_eval\qs_eval CollidingMice.js
```

窗口弹出、老鼠碰撞动画正常运行，即说明脚本对 Qt 类（QGraphicsScene、QPainter、
定时器、信号槽等）的引用全部有效。其它示例同理，例如：

```
..\qtbindings\qs_eval\qs_eval TwoWayButton.qs    :: 20 行的状态机按钮
..\qtbindings\qs_eval\qs_eval AnalogClock.js     :: 模拟时钟
..\qtbindings\qs_eval\qs_eval Wiggly.js          :: 文字抖动动画
```

其中 `TwoWayButton.qs` 最小（完整源码见文件）：用 `QStateMachine` + `QState` +
`assignProperty` + `clicked()` 信号转移，实现一个 On/Off 切换按钮——
如果它能正常弹出并响应点击，就证明核心/状态机/控件这条绑定链路是通的。
