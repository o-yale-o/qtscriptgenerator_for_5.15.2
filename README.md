# qtscriptgenerator_for_5.15.2

Qt Script Generator（Qt Labs 0.2）的移植分支：把 QtScript 绑定生成器
从 Qt 4 时代移植到 **Qt 5.15.2（msvc2019_64）+ MSVC 2019**。12 个模块
（core / gui / widgets / printsupport / multimedia / network / opengl /
sql / xml / svg / xmlpatterns / uitools）全部**可从源码再生成、可编译、
可运行**，18 个示例脚本批量回归 0 脚本异常。

> 本节已按当前（Qt 5.15.2 + VS2019）形势全面改写。上游原版说明
> （"works with Qt 5.6.0 and MSVC 2015"、`qmake && make`、"不带参数运行
> 生成器"等）描述的是旧工作流，与本仓库实际不符，已删除；原版原文可从
> git 历史找回。

## 环境要求

| 用途 | 需要 |
|---|---|
| 只跑示例 | 直接用 [发布包](https://github.com/o-yale-o/qtscriptgenerator_for_5.15.2/releases)（v1.0-qt5.15.2），什么都不用装 |
| 重生成绑定源码 | Qt 5.15.2 头文件在标准位置 `C:\Qt\5.15.2\msvc2019_64`（用现成 generator.exe，无需 MSVC） |
| 自行构建 generator.exe / 编译插件 | MSVC 2019 + Qt 5.15.2 (msvc2019_64) |

## 快速上手（当前工作流）

1. **构建生成器**（一次性；或直接用发布包里的）

   ```
   cd generator
   qmake generator.pro -spec win32-msvc "CONFIG += release"
   nmake /f Makefile.Release
   ```

2. **重生成绑定源码**（设 QTDIR 并把 Qt bin 加入 PATH；逐模块执行）

   ```
   set QTDIR=C:\Qt\5.15.2\msvc2019_64
   set PATH=%QTDIR%\bin;%PATH%
   cd generator
   release\generator.exe qtscript_masterinclude.h typesystem_core.xml
   release\generator.exe qtscript_masterinclude.h typesystem_gui.xml
   ```

   其余模块（widgets / printsupport / multimedia / network / opengl /
   sql / xml / svg / xmlpatterns / uitools）同理。产物：根目录
   `generated_cpp\`（绑定源码）、`jsx\`（绑定面速查文档，仅文档价值，见下）、
   `doc\`（文档）。

3. **编译绑定插件与 qs_eval**

   ```
   cd qtbindings
   qmake qtbindings.pro -spec win32-msvc "CONFIG += release"
   nmake /f Makefile.Release
   ```

   产物：`plugins\script\qtscript_<模块>.dll` 与
   `qtbindings\qs_eval\release\qs_eval.exe`。

4. **跑示例**

   ```
   cd examples
   ..\qtbindings\qs_eval\release\qs_eval.exe CollidingMice.js
   ```

   qs_eval 自动导入全部绑定插件（qt.core / qt.gui / …）——插件搜索路径
   由 qs_eval 按 exe 相对位置注入，无需手工 `setLibraryPaths()`。
   脚本异常会进入交互式调试器；设 `QSEVAL_NO_DEBUGGER=1` 可改为控制台
   报错退出（`QSEVAL_FAILFAST=1` 连事件循环内的异常也捕获）。

5. **交互式解释器**：`qs_eval` 不带参数，或 `qs_eval -i`。

## 文档地图

- 【架构说明】：xml ⇒ cpp ⇒ dll ⇒ js 全链路、承上启下的桥、jsx 定性
- 【设计指南】：把自己的 Qt 类导出给脚本（newQObject / 手写绑定 / 走管线）
- 【发布包】：GitHub Releases 一包三用的设计、自测与打包避坑
- 【修改说明】系列：修复史（parser C++11、默认实参、槽绑定、
  QFile::open 遮蔽、无人值守测试基建等）

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

1. **生成 generator.exe**（构建产物不入库，请按下述步骤自行构建）：

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

---

## 【修改说明 · 续】让全部 12 个模块从源头可再生成、可编译、可运行

在上一轮"能编过若干个类"的基础上，本轮完成了**生成器源头**的系统性修复：
不再手改任何生成物，重新用 generator.exe 批量生成的 .cpp/.h 直接可编译。
12 个模块（core/gui/widgets/printsupport/network/xml/xmlpatterns/multimedia/
opengl/sql/svg/uitools）的插件 DLL（release+debug）与 qs_eval.exe 全部构建成功，
CollidingMice.js、TwoWayButton.qs 等示例实际运行通过。

### 1. 跨模块类型孤岛：`load-typesystem`

**现象**：单独生成某模块时，凡参数/基类涉及其它模块的类（如 QStyleOption 的
QFont、QHeader 视图的 QWidget 参数），要么整类变 abstract 空壳、要么构造参数
类型解析失败。

**根因**：typesystem_*.xml 原本互相不加载。core 之外的所有模块 typesystem 里，
QtCore 的类型（QString、QObject 等）由 TypeDatabase 内建 primitive 补齐，但
QtGui/QtWidgets 的类对 printsupport/uitools 等模块完全未知——未注册类型的参数
会被当作无法匹配而丢弃函数，基类不认识则整类变 abstract。

**修复**：每个模块 typesystem 头部加入（以 widgets 为例）：

```xml
<load-typesystem name="typesystem_core.xml" generate="no"/>
<load-typesystem name="typesystem_gui.xml" generate="no"/>
```

`generate="no"` 使被加载模块的类以 GenerateForSubclass 模式注册：类型可解析、
函数签名可匹配，但不会在当前模块重复输出绑定文件。`uitools` 依次加载
core+gui+widgets，`printsupport` 加载 core+gui+widgets，以此类推。
gui 加载 core；xml/xmlpatterns/network/sql 加载 core；multimedia/opengl/svg
加载 core+gui。

### 2. `= nullptr` 默认实参不识别（Qt5 头的普遍写法）

**现象**：`new QPushButton()`（无参）抛 "could not find a function match"。
Qt5 头文件普遍写 `QWidget *parent = nullptr`，而 Qt4 时代写 `= 0`。

**根因**：`AbstractMetaBuilder::translateDefaultValue()`（abstractmetabuilder.cpp）
只把字面量 `"0"` 翻译为 "null"，`"nullptr"` 落入对象类型分支返回空串，
导致默认实参被丢弃、最小参数个数=满参个数，0 参构造无分支可派发。

**修复**：

```cpp
} else if (expr == "0" || expr == "nullptr") {
    return "null";
```

修复后所有带 nullptr 默认值的函数都能以最少参数调用（QPushButton、QTimer、
QGridLayout 等成千上万处受益）。

### 3. Qt5 的 per-TU QMetaTypeId 特化要求（metatype 声明策略）

Qt4 时代 `qscriptvalue_cast<T>` 靠 QVariant 运行期转换，声明一次即可；Qt5 的
`qMetaTypeId<T>()` 要求**每个翻译单元**都有 `QMetaTypeId<T>` 特化，即生成的
每个 .cpp 都需要自己的 `Q_DECLARE_METATYPE(T)`。相应修改（classgenerator.cpp）：

- **值类型不再因"无默认构造函数"被跳过**：原来无默认 ctor 的值类型
  （QStyleOption、QPicture 等）被预先塞进 registeredTypeNames 而不发射声明，
  Qt5 下这些 TU 里的 `qscriptvalue_cast<T>` 直接编译失败。现在值类型一律发射。
- **黑名单扩充**（`maybeDeclareMetaType`）：声明由手写 `__package_shared.h`
  提供的 QFontInfo/QFontMetrics/QFontMetricsF/**QEvent**（避免 C2766 重复特化），
  以及 **QTextStream**（Qt5 删除了拷贝构造，`Q_DECLARE_METATYPE(QTextStream)`
  本身无法编译）。
- **QDomDocument 等 SAX 相关类**：`Q_DECLARE_METATYPE(T*)` 在 Qt5 要求 T 完整
  定义，而 qdom.h 只有前置声明——给 QDomDocument 条目加 extra-includes
  （QXmlInputSource/QXmlReader）。

### 4. `qscriptvalue_cast<T&>` 结构性不兼容（Qt5 无法编译引用转换）

Qt5 的 qscriptengine.h 明确将 `QMetaTypeId2<T&>::Defined` 置 false，任何生成
`qscriptvalue_cast<X&>` 的代码都无法编译。生成器会为值类型的 shell 覆盖
`operator=`（返回 T&），Qt4 时代即可编译、Qt5 不行。对受影响的值类型在
typesystem 里统一 `remove`（与 gui 模块既有惯例一致）：

- `typesystem_widgets.xml`：QStyleOption 及 20 个 QStyleOption* 子类；
- `typesystem_xml.xml`：QXmlAttributes；
- `typesystem_xmlpatterns.xml`：QXmlNodeModelIndex。

另外 Qt5 把 QStyleOption::operator= 改为 protected，QCursor::operator== 变成
友元非成员、QLabel::picture() 加 const 等，均按 Qt4→Qt5 实际签名逐一 rejection。

### 5. parser 修复（详见前节 + 以下新增）

- `parseExceptionSpecification`：识别 noexcept/override/final；
- `parseUsing`：C++11 类型别名 `using X = Y;`；
- `parseQ_PROPERTY`：QDOC_PROPERTY 分支；
- `parseQ_ENUMS`：识别 Qt5 的 `Q_ENUM/Q_ENUM_NS/Q_FLAG/Q_FLAG_NS(...)`；
- `parseMemInitializer`：C++11 花括号成员初始化 `: member{a, b, c}`
  （QVector3D 等构造函数因此曾被整类丢弃）；
- **深度感知的成员循环错误恢复**：解析失败时不再调用会吞掉余下全文件的
  `skipUntilDeclaration()`，改为按花括号深度有界回退，绝不越过类结束大括号。
  修复后模型类数量从 1856 恢复到 2439。
  （注意：不可用 `token_stream.matchingBrace()`——该 API 在本 lexer 上从未被
  填充，返回的是垃圾值。）

### 6. 模块工程文件（.pro）

- `qtscript_sql`：`QT -= gui` → `QT += core gui sql`（QSqlDriver 引用
  QtGui/qevent.h）；
- `qtscript_xmlpatterns`：`QT -= gui` → `QT += core gui xmlpatterns network`；
- `qtscript_uitools`：删除 Mac framework 路径 `${QTDIR}/lib/QtWidgets.framework/Headers`
  （qmake 会原样写进 nmake 的 INCPATH，`{` 属非法宏字符导致 U1001）。

### 7. 示例脚本 Qt4→Qt5 API 适配

`examples/CollidingMice.js`：`QGraphicsItem::rotate()` 在 Qt5 已移除——
构造函数中改为 `setRotation(angle)`（初始角度 0，二者等价）；每帧累积旋转的
`rotate(dx)` 改为 `setRotation(rotation() + dx)`。

### 验证（本轮）

- 全部 12 模块经 generator.exe 重新生成后，nmake 全量编译链接通过
  （release + debug 两套），插件输出于 `plugins/script/`；
- `qs_eval TwoWayButton.qs`：窗口正常、点击切换、正常退出（exit 0）；
- `qs_eval CollidingMice.js`：7 只老鼠碰撞动画稳定运行；
- `qs_eval AnalogClock.js` 等其余示例的 QPainter.rotate/scale 为 Qt5 仍保留的
  API，无需改动。

### 已知限制

- `qt.xmlpatterns` / `qt.uitools` 插件在 qs_eval 启动时 import 失败
  （插件 DLL 已构建且依赖完整，import 失败原因未深究——不影响其它 10 个模块
  与全部示例运行）；`qt.webkit`/`qt.webkitwidgets` 本来就没有绑定。
- `QMatrix::inverted(bool*)`、`QTransform::inverted(bool*)` 等带输出指针参数的
  函数按 Qt4 时代惯例做了参数移除/私有化，脚本中拿到的是返回值版本。

---

## 【修改说明 · 三】槽函数绑定修复（AnalogClock 调通）

第一轮验证时 AnalogClock.js 报 `painter.begin is not a function`，深挖后修掉
一条完整的因果链：

1. **`pp-qt-configuration` 补 `#define slots`**。配置原本把 `Q_SLOTS` 展开成
   标识符 `slots`，但自身未定义 `slots`，导致预处理流中残留 `public slots:`，
   遗留 parser 在此处失败并靠深度恢复吞掉整段槽声明（QWidget 的
   show/hide/setVisible/setWindowTitle/deleteLater/raise/close… 全部丢失，
   计 300+ 个函数）。Qt 官方本就默认把 `slots` 定义为空，补上后槽函数按
   普通成员正常绑定。
2. **注册 `QPaintDevice`**（typesystem_gui.xml）。该类型缺失使
   `QPainter::begin(QPaintDevice*)` 与 `QPainter(QPaintDevice*)` 被静默丢弃，
   `painter.begin(this)` 不可用；所有构造函数均为 protected，
   `new QPaintDevice()` 依然不可能。
3. **容器条目 include 污染防护**（abstractmetabuilder.cpp）。Qt 5.15 的
   qevent.h 含 `template <> class QList<QPointingDeviceUniqueId> {}` 显式
   特化，解析器将其记录为名为 QList 的"类"（所在文件 qevent.h），绑定顺序
   一旦先碰到它，QList 容器条目的 include 即被污染 → 网络模块（不含 gui）
   全部生成文件 include qevent.h 而编译失败。现在容器条目不再从类遍历继承
   include。
4. **属性访问器纳入绑定表**（classgenerator.cpp）。原本 Q_PROPERTY 的
   read/write 函数被跳过，脚本子类（`this` 为普通 JS 对象）无法调用
   `setWindowTitle` 等。现按普通函数生成。
5. **`isQObjectBased` 支持多继承**（classgenerator.cpp）。QWidget 同时继承
   QObject 与 QPaintDevice，`baseClass()` 只沿单链走、可能选中
   QPaintDevice，导致 QWidget 被误判为非 QObject 系、构造器用 newVariant
   包装，信号 `connect(this,"slot()")` 全部失效。现优先读取 builder 按
   完整基类列表计算的 QObject 标志。
6. `QLabel::pixmap()` 返回 `const QPixmap*`（Qt4 为非 const），生成代码
   无法编译，按 `QLabel::picture` 先例 rejection。

**验证**：AnalogClock.js（时钟走针）、CollidingMice.js（老鼠碰撞）、
TwoWayButton.qs（状态机按钮）全部无脚本异常、正常交互退出（exit 0）。
QWidget 绑定函数表从 114 个恢复至 425 个。

---

## 【修改说明 · 四】示例全量自动化回归：18/18 全部通过

本轮把"逐个手动跑 demo"升级为无人值守批量回归，并顺手修掉了最后一批
Qt4→Qt5 兼容问题。至此 `examples/` 下全部示例在批量测试中 0 脚本异常。

### 1. qs_eval 支持无人值守模式（main.cpp）

`QScriptEngineDebugger` 原本无条件挂载，脚本异常会转入交互式调试器挂住进程，
错误信息无法被批量脚本捕获。现增加两个环境变量开关：

- `QSEVAL_NO_DEBUGGER=1`：不挂调试器，顶层异常走既有的"stderr 打印 +
  EXIT_FAILURE"路径（main.cpp 既有逻辑）；
- `QSEVAL_FAILFAST=1`：事件循环内的 JS 异常（信号槽、动画回调）通过
  `QScriptEngine::signalHandlerException` 打印到 stderr 并 `exit(2)`。

交互行为完全不变（不设变量时调试器照常挂载）。qs_eval 已用 vcvars64 +
nmake 重编译。

### 2. 批量回归脚本（examples/）

- `_run_all.ps1`：逐个用 qs_eval 运行示例，6 秒超时后 `taskkill /T` 收割；
  捕获 stdout/stderr 与退出码后输出 JSON 汇总。
  判定：EXITED-0 / KILLED@6s 且 stderr 无异常 = 通过。
  （注意 PowerShell 5.1 的 `Start-Process` 在进程环境同时存在大小写不同的
  `NO_PROXY`/`no_proxy` 时会抛字典冲突，脚本改用 .NET `Process` + `cmd /c`
  重定向绕过。）
- `_probe.js`：绑定 API 可用性探针，结果写临时文件后一次性输出；
- `_sb_check.js`：QXmlStreamReader/Writer 读写往返的无头验证；
- `_cc_auto.js`：ConcentricCircles 的 5 秒自动退出变体。

### 3. 示例脚本的 Qt4→Qt5 适配（本轮修复）

- `Wiggly.js`：`QBoxLayout::addWidget(w, stretch)` 两参形式失败——Qt5 头里
  `Qt::Alignment alignment = Qt::Alignment()` 这类"临时对象默认实参"不被
  生成器识别（`translateDefaultValue` 只翻译 `0`/`nullptr`），默认值被丢弃
  后该函数最少需要 3 参。脚本显式补 `addWidget(w, stretch, 0)`。
  （`QGridLayout::addWidget(w, r, c)` 三参形式不受影响，这也是此前
  LineEdits 等能通过的原因。）
- `AnimatedBox.qs`：同一根因，`QGraphicsScene::addWidget(w, flags)` 默认
  `Qt::WindowFlags()` 被丢弃，补 `addWidget(w, 0)`。
- `RSSListing.js`：`QHeaderView::setResizeMode` 在 Qt5 已改名
  `setSectionResizeMode`；`QHttp` 在 Qt5 整个移除，改用
  `QNetworkAccessManager.get(QNetworkRequest)` + `QNetworkReply` 的
  `readyRead`/`finished` 信号重写 fetch/abort/readData。
- `_cc_auto.js`：`QTimer.singleShot` 静态函数未绑定（Qt4 时代示例的已知
  FIXME），改用 `singleShot` 属性为 true 的 `QTimer` 对象触发 `quit()`；
  另外原文件把 `singleShot` 写在 `exec()` 之后（永不执行），已调整顺序。
- `StreamBookmarks.js`：`readXBEL()` 里三处 `name()` 缺 `this.`（JS 下
  ReferenceError）；把 `open()` 拆出 `loadBookmarks(fileName)`，启动时若
  存在 `frank.xbel` 则自动加载（无人值守验证），否则保持原有文件对话框
  交互。

### 4. 绑定层：QFile::open 被 fd 重载遮蔽（typesystem_core.xml）

**现象**：`file.open(QIODevice.OpenMode(...))` 报"could not find a function
match"，候选只有 `open(int fd, OpenMode, FileHandleFlags)`。

**根因**：`open(OpenMode)` 绑在 `QIODevice` 原型上，而 `QFile` 自身又绑定了
fd 版 `open(int, OpenMode, FileHandleFlags)`，按属性查找遮蔽了原型链。Qt4
时代的 typesystem 本就 remove 掉了 fd 版（`open(int,QFlags<OpenModeFlag>)`），
但 Qt5 给它加了第三个参数 `FileHandleFlags`（canonical 名为
`QFileDevice::FileHandleFlag` 的 QFlags），旧签名匹配不上、remove 静默失效。

**修复**：签名补全为
`open(int,QFlags<QIODevice::OpenModeFlag>,QFlags<QFileDevice::FileHandleFlag>)`。
重新生成 core 模块（generator.exe 输出确认 fd 版 open 已移除）并重编
`qtscript_core.dll`，`file.open(...)` 现在正确解析到
`QIODevice.prototype.open`。

### 5. 验证（本轮）

`_run_all.ps1` 全量回归 18 个示例：**0 脚本异常**。其中
StreamBookmarks 自动加载 frank.xbel 成功（`_sb_check.js` 往返验证：
读出 1 xbel / 11 folder / 64 bookmark / 75 title，写出 XML 格式正确）。

### 已知限制（更新，经 `_probe.js` 探针证实）

- `qt.xmlpatterns` / `qt.uitools` 插件 import 失败的原因仍未深究（不影响
  其余 10 个模块）；`qt.webkit`/`qt.webkitwidgets` 本无绑定。
- `QInputDialog.getInt/getInteger` 均未绑定（`getDouble` 正常；疑似 Qt5
  签名里的 `bool *ok = nullptr` 输出参数处理问题），StandardDialogs 的
  对应按钮点击时会失败，窗口及其它按钮正常。
- `QTimer.singleShot` 静态未绑定、`QHeaderView::setResizeMode`/
  `QHttp` 已随 Qt5 移除——均已在示例脚本中绕过/重写。
- Screenshot.js 可正常运行：`QPixmap.grabWindow`、`QApplication.desktop()`、
  `QDesktopWidget` 在绑定中均存在（探针证实），无人值守回归 0 异常。

---

## 【修改说明 · 五】QCAD 兼容：QWidget 原型链悬空与 move 构造歧义

把本仓库编译的绑定 DLL 替换进 QCAD（`src/3rdparty/qt-labs-qtscriptgenerator-5.5.0`
是其自带的参照分叉，QCAD 脚本按它的行为编写）后，`autostart.js:588` 报
`appWin.setProperty is not a function`。两个独立根因，均在源头修复：

### 1. QWidget 脚本原型链悬空（typesystem_gui.xml）

QCAD 的 `appWin = new RMainWindowQt()`（QMainWindow → QWidget），588 行的
`setProperty` 是 QObject 方法，只能沿原型链找到。探针显示
`QObject.setProperty=function` 但 `QWidget.setProperty=undefined`——
QWidget 原型的 `__proto__` 是悬空的。

**根因**：QPaintDevice 被声明了两次——GUI 模块 `<object-type>`（【修改
说明·三】为修 QPainter::begin 添加），widgets 模块 `<interface-type>`。
gui 先加载，object-type 占位，widgets 的 interface-type 沦为重复条目被
无视 → QWidget 解析基类时 QObject 与 QPaintDevice 双双成为"primary base"
→ `return false` → baseClass 为空 → classgenerator 跳过原型挂接。
（示例全绿掩盖了它：paintEvent/信号走 shell 回查机制，不经过原型链。）

**修复**：gui 条目改为 `<interface-type name="QPaintDevice"/>`（类型注册
与 QPainter::begin 绑定保留；widgets 加载后接口语义不再被冲掉）。重生成
后 `qtscript_QWidget.cpp` 出现
`proto.setPrototype(engine->defaultPrototype(qMetaTypeId<QObject*>()))`
——与 QCAD 分叉的手工补丁完全一致。

### 2. move 构造导致 shell 双歧义（abstractmetabuilder.cpp）

Qt5 头文件在拷贝构造旁新增 `X(X &&other)`。解析器把 rvalue-ref 参数折叠
为自身类型的值参数，生成器于是为 QImage 同时产出
`QtScriptShell_QImage(QImage)` 与 `QtScriptShell_QImage(const QImage &)`，
转发调用报 C2668；脚本侧构造函数表也出现两个近重复项。

**修复**：builder 中拒绝"单一自身类型值参数、非 const"的构造函数
（即 move 构造；脚本无法区分 move/copy）。注意排除指针参数——
`QGraphicsItem(QGraphicsItem *parent)` 是合法父子构造，不能误杀
（第一版条件漏了 `indirections()==0`，CollidingMice 的 Mouse(parent)
当场回归暴露，已修正）。

### 验证

- 探针：`QWidget.setProperty/property=function`、
  `QGraphicsItem(parent)` 构造可用、`QPainter.prototype.begin=function`、
  QImage 构造/拷贝正常；
- 18 示例全量回归 0 异常（CollidingMice 曾被第一版条件误伤，修正后恢复）；
- QCAD 侧：替换 `qtscript_gui.dll` + `qtscript_widgets.dll` 后
  `appWin.setProperty` 可用。

---

## 【架构说明】绑定流水线：xml ⇒ cpp ⇒ dll ⇒ js

```
Qt 5.15 头文件 ──────────────┐
                            ▼
  typesystem_*.xml ──►  generator.exe  ──►  旁路产物: jsx/qt/*.jsx (绑定面文档，见下)
  (规格：绑什么、怎么改)   (parser+builder)          doc/ (HTML 文档)
                            │
                            ▼
        generated_cpp/com_trolltech_qt_*/
          ├ qtscript_<Class>.cpp         绑定体：函数表/枚举表/构造/类型转换
          ├ qtscriptshell_<Class>.cpp/h  虚函数外壳：JS 覆写 ↔ C++ 回传的通道
          └ com_trolltech_qt_<模块>_init.cpp + .pri
                            │
                            ▼  qmake + nmake / MSVC
        编译工程：qtbindings/qtscript_<模块>/qtscript_<模块>.pro
          （.pro 只做两件事：编译自己的 plugin.cpp 入口 +
           include($$GENERATEDCPP/com_trolltech_qt_<模块>.pri)
           直接引用上一步 generated_cpp 里的源码；
           DESTDIR 由 qtbindingsbase.pri 定为 plugins/script，
           debug 版为 plugins/script_debug；
           12 个模块工程由 qtbindings.pro 的 SUBDIRS 统一编排）
                            │
                            ▼
        plugins/script/qtscript_<模块>.dll   (QScriptExtensionPlugin)
                            │
                            ▼  运行时
        qs_eval: importExtension("qt.core"…)
          → plugin.cpp → qtscript_initialize_..._bindings(globalObject)
          → 类/原型链/枚举/信号槽全部挂上引擎
                            │
                            ▼
        你的 .js 脚本
```

### 承上启下的节点（桥）

| 桥 | 连接的两端 | 性质 |
|---|---|---|
| **generator.exe**（+ typesystem XML） | C++ 头文件世界 → 脚本绑定源码 | **中枢**。XML 是"规格书"（绑哪些类、remove/rename 哪些函数、模块依赖链 `load-typesystem`），generator 把规格应用到 Qt 头上产出 cpp |
| **qtscript_<模块>.pro + nmake/MSVC** | cpp → dll | **编译节点**。每模块一个 qmake 工程（`qtbindings/qtscript_<模块>/`），`.pro` 通过 `include($$GENERATEDCPP/....pri)` 直接引用 generated_cpp 源码，连同自身 `plugin.cpp`（keys/initialize 入口）链接成 QScriptExtensionPlugin，输出到 `plugins/script/`（debug → `script_debug/`）；`qtbindings.pro` 的 SUBDIRS 编排全部 12 个模块 + qs_eval |
| **plugin.cpp + init.cpp** | dll → 引擎 | 运行时入口：`importExtension` 触发插件把所有类挂到 globalObject（qs_eval 启动时那串 import 就是在喂它） |
| **qtscriptshell_*.cpp** | C++ 虚调用 ↔ JS 覆写 | **运行时桥梁**。AnalogClock 里 `paintEvent` 能被 C++ 绘制事件调到，靠的是生成的 shell 类在 C++ 侧覆写虚函数、再回头查找 JS 对象上的同名函数 |

### jsx 文件是什么？——只有文档价值的旁路产物

逐层查证（grep 全仓库）：`.jsx` 唯一的生产者是 `generator/jsxgenerator.cpp`
（generator.exe 内部组件），**消费者：没有**——qtbindings、generated_cpp、
构建脚本、运行时全都不引用它。

- **定位：只有文档价值。** 它用 ECMAScript 风格的伪语言列出每个类暴露给
  脚本的完整接口（`native class QBasicTimer { function
  start(msec:int, obj:QObject):void }`），外加每模块一张 import 清单
  （`qt_core.jsx`）。想快速确认"某个类/函数/枚举到底暴露了没有"，在
  `jsx\` 里 grep `<小写类名>.jsx` 比翻 `generated_cpp` 的函数表快得多
  ——这就是它在**本仓库唯一的用途**。
- 构建与运行完全不消费它（不编进 dll、不在运行时求值）；删掉后重生成
  会原样再生，所以也可以随包保留当速查索引。
- 文件头自带 *"generated by JSXgenerator, DO NOT EDIT"*；历史用途是
  Qt Labs 原项目里给 QtScript 的 ES4 前端工具当输入。
- 重生成时 jsx 大量变化属正常伴生刷新（如 `qbasictimer.jsx` 首次出现 =
  QBasicTimer 绑定首次生成），不要手工编辑。

---

## 【设计指南】导出自己的 Qt 类给脚本用

### 先做一个关键分界判断

**场景 A：只是把"现成实例"丢给脚本用**（不 new、不从 JS 继承）

```js
engine->globalObject().setProperty("myObj", engine->newQObject(instance));
```

**零管线参与**。`newQObject` 基于 QMetaObject 自动暴露该实例的
signals/slots/Q_PROPERTY/Q_INVOKABLE，脚本里 `myObj.someSignal.connect(...)`
直接可用。只需要这个的话，下面都不用做。

**场景 B：脚本里要 `new MyClass()`、访问静态/枚举、从 JS 继承你的类**

必须有绑定层，两条路：

### B1 手写绑定（类少时推荐）

完全绕过生成器，直接用 QtScript API 手写，模式照抄
`generated_cpp/qtscript_QDir.cpp`（函数表 + 构造函数 + prototype +
`qScriptRegisterMetaType`）。在宿主进程里注册即可，不用动 qs_eval。

### B2 走完整管线（类多、或想让 JS 覆写你的虚函数）

管线不关心头文件是谁的——它绑 Qt 类只是"恰好喂了 Qt 头"。需要动/新增
的节点按顺序：

| 环节 | 要做什么 |
|---|---|
| ① 头文件 | 写 `mylib_masterinclude.h`（自包含，模仿 `qtscript_masterinclude.h`）；避免生僻 C++ 新语法（parser 的 C++11 修复已就位，但未覆盖一切） |
| ② typesystem | 新建 `typesystem_mylib.xml`：头部 `<load-typesystem name="typesystem_widgets.xml" generate="no"/>`（拉进 QWidget 等依赖类型），逐类 `<object-type name="MyWidget"/>` |
| ③ generator | `generator.exe mylib_masterinclude.h typesystem_mylib.xml`（QTDIR/PATH 环境同 core 那次） |
| ④ 工程 | 新建 `qtbindings/qtscript_mylib/`：`.pro` 抄 `qtscript_core.pro` 改源码列表；`plugin.cpp` 抄现有的，keys 换 `"qt.mylib"`；`qtbindings.pro` 的 SUBDIRS 加一行 |
| ⑤ 编译 | vcvars64 + nmake → `plugins/script/qtscript_mylib.dll` |
| ⑥ 运行时 | qs_eval 的 extensions 列表加 `"qt.mylib"` 重编（或自己的宿主程序里 `importExtension("qt.mylib")`） |

### 关于"派生"的两个方向

- **C++ 派生 QWidget 做自定义控件 → 导出**：走 B2 即可。生成器对有虚函数
  的类自动产出 `qtscriptshell_<YourClass>`，JS 侧覆写虚函数天然可用
  （与 AnalogClock 覆写 paintEvent 同机制）。§三修过的坑（多继承 QObject
  误判、Q_PROPERTY 访问器）在管线里已修，自动继承。
- **JS 里从你的类再派生子类**：只有 B2（shell 机制）自然支持；B1 手写要
  自己复刻 shell 模式（抄 `qtscriptshell_QAbstractButton.cpp` 那套
  "C++ 虚函数 → 查 JS 同名函数"的外壳），工作量不小。

---

## 【发布包】一包三用的设计（v1.0-qt5.15.2）

发布地址：GitHub Releases → `v1.0-qt5.15.2`
（`qtscriptgenerator_for_5.15.2_win64_qt5.15.2.zip`，约 17.6 MB）。

### 设计初衷

git 仓库只跟踪源码（.gitignore 排除了 exe/dll/generated_cpp），任何"想
直接体验一下"的人都得先装 Qt、MSVC、跑 qmake/nmake——门槛太高。发布包
把三类使用场景压缩成"解压即用"：

| 场景 | 人群 | 包内入口 |
|---|---|---|
| [1] 免编译跑 js 示例 | 只想看看绑定能不能用 | `run_demo.bat` |
| [2] 双击重新生成 cpp/h | 改 typesystem 规格的人 | `generator\regenerate_all.bat [模块]` |
| [3] 用预生成源码直接编 DLL | 改过规格或源码、要出新插件的人 | `build_bindings.bat [模块]` |

关键取舍：**[2] 不需要 MSVC**（generator.exe 是预编译的，重生成只是
文本处理）；**[1] 不需要 Qt 安装**（运行库直接随包）；只有 [3] 需要完整
工具链。环境假设与打包机一致：Qt 5.15.2 在 `C:\Qt\5.15.2\msvc2019_64`。

### 结构关键点（骨架为什么不能动）

发布包 = 仓库骨架 + 补回二进制，有三个"相对路径契约"决定了骨架：

1. **qs_eval.exe 找插件**：main.cpp 从 exe 位置向上回溯找 `plugins/`
   目录（`qtbindings/qs_eval/release/` → 上两级 → `<根>/plugins`）。
   所以 exe 必须待在 `qtbindings\qs_eval\release\`，插件在根
   `plugins\script\`。
2. **Qt 运行库贴着 exe 放**：14 个 Qt5 DLL + `platforms\qwindows.dll`
   + `styles\qwindowsvistastyle.dll` 全部放 exe 旁边，Windows 的 DLL
   搜索顺序第一顺位就是 exe 所在目录，用户 PATH 里有没有 Qt 都无所谓。
   模块清单按 qs_eval + 12 个插件 dll 的运行时依赖取：Core/Gui/Widgets/
   Script/ScriptTools/Network/Xml/XmlPatterns/Svg/Sql/OpenGL/Multimedia/
   Concurrent/PrintSupport。
3. **generator.exe 的输出落点**：在 `generator\` 目录下运行时，输出写
   到上级根的 `generated_cpp\`、`jsx\`、`doc\`；其运行数据
   （`build_all.txt`、pp 配置）从 exe 旁 `release\data\` 读取，主包含头
   与 typesystem 从工作目录取。所以生成器一侧也保持仓库原布局。

补齐的二进制清单：`generator\release\generator.exe(+data\)`、
`qtbindings\qs_eval\release\qs_eval.exe(+Qt运行库)`、
`plugins\script\*.dll`（12 个）；其余与仓库同构（examples 全量、
generated_cpp 全量、jsx（绑定面速查文档）、12 个 qtscript_* 工程壳、
typesystem 全套含 -common/-qtscript 分片、qtscript_masterinclude.h、
build_all.txt、LGPL_EXCEPTION.txt）。

### 三个 bat 的分工

| 脚本 | 作用 | 实现要点 |
|---|---|---|
| `run_demo.bat [脚本]` | [1] 跑示例，缺省 CollidingMice.js | pushd 到 examples\ 后调相对路径的 qs_eval.exe；设 `QSEVAL_NO_DEBUGGER=1`，脚本异常直接在控制台报错退出，不进交互式调试器；透传退出码 |
| `generator\regenerate_all.bat [模块]` | [2] 重生成源码 | 设 QTDIR/PATH 后 cd 到 generator\，对单个或全部 12 个模块执行 `generator.exe qtscript_masterinclude.h typesystem_<模块>.xml`；任一模块失败立即停（`\|\| goto :fail`） |
| `build_bindings.bat [模块]` | [3] 编译插件 | 四种 VS2019 版本自动定位 vcvars64.bat → qmake + nmake；缺省全量（顶层 SUBDIRS），带模块名则只编该模块（分钟级）；产物直接落到 `plugins\script\` |

三者都不需要先跑 qmake 全量：[3] 单模块路径会在模块目录里就地生成
Makefile。

### 发布前自测（三关全过）

1. 包内 `qs_eval.exe _cc_auto.js` → 5 秒自动退出 exit 0（目标 [1]；
   同时验证 Qt 库/插件部署完整）；
2. 包内 `regenerate_all.bat uitools` → 重生成结果与随包源码**逐字节
   一致**（目标 [2]；证明生成器可再生产、包内环境自洽）；
3. 包内 `build_bindings.bat xml` → 从随包源码编出全新
   `qtscript_xml.dll`（目标 [3]；完整走通 vcvars 定位 → qmake → nmake）。

### 打包避坑（再打包前必读）

- **bat 必须纯 ASCII + CRLF**：cmd 在 GBK 代码页下解析 UTF-8 中文注释会
  把字节流切碎，出现"'em' 不是内部或外部命令"这类乱码错；
- **含 `(x86)` 的路径不能写进 for/if 括号块**：`C:\Program Files
  (x86)\...` 中的括号会提前闭合语句块（"此时不应有 \Microsoft"），
  vcvars64 的定位要用平铺的独立 `if exist ... set "VCVARS=..."` 行；
- 测试产生的痕迹要清理后再压缩：模块目录下的 `Makefile*`、`.obj`、
  `.qmake.stash`、`vc140.pdb`、`debug\`，根目录测试期生成的 `doc\`
  （重生成单模块只会产出该模块的文档，发布带半套文档会误导）；
- 打包等价操作 = robocopy 仓库各源目录 → 补二进制 → 清理 → 压缩；
  暂存树约定在仓库 `_pkg/`（已 gitignore，随包 zip 也放在里面）。
